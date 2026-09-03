#!/usr/bin/env python3
"""Standalone Open-Coordinator-Backup client for the ZiGate JN5169 firmware.

Talks the ZiGate SerialLink framing directly over the coordinator's serial port
(outside zigbee2mqtt / zigbee-herdsman, which have no ZiGate backup support) and
drives the EXPERIMENTAL OCB key export + streamed restore ABI added to this
firmware. It produces / consumes a JSON file shaped like
zigpy/open-coordinator-backup.

    backup:   ocb_backup.py backup  --port /dev/ttyUSB0 --out net.json
    restore:  ocb_backup.py restore --port /dev/ttyUSB0 --in  net.json [--adopt-ieee]

REQUIREMENTS
------------
* The coordinator must run a firmware built with
  OCB_KEY_EXPORT_RESTORE_EXPERIMENTAL=1. A default image exports only metadata
  and cannot restore.
* pyserial (`pip install pyserial`).

SAFETY / LIMITATIONS (see docs/OCB_UART_ABI.md; HIL-tested 2026-08-31)
------------------------------------------------------------------------
* The 30-second unlock is an accidental-invocation guard, NOT authentication:
  anyone with the UART can read the network key. Handle the JSON as a secret.
* Still experimental/unqualified: no atomic rollback across COMMIT (keep the
  unit powered), and it never sets the reserved BackupCapable bit. HIL testing
  verified network key, PAN/ext-PAN id, channel, TC link key, and the NWK
  outgoing frame counter all restore correctly and survive the COMMIT reboot,
  including onto a target whose PDM had just been fully erased (factory-new).
  The frame counter needed a non-obvious fix (root-caused by disassembling
  libZPSNWK_JN516x.a: it's persisted via a PDM *bitmap* record, not a plain
  value) and can add a few seconds to a restore.
* Per-flash-TCLK APS frame counters are not restorable; restored keys re-sync
  their APS counter on the next exchange.
* --adopt-ieee overrides the target's MAC IEEE with the source's so devices
  keep their addresses after a physical coordinator swap. An earlier build
  reliably hung boot when this was applied (root-caused by disassembling
  libZPSMAC_Mini_SOC_JN516x.a / libMiniMac_JN5169.a to a hardware MAC register
  write reached before the radio was initialised); fixed by moving that
  boot-time call to after ZPS_eAplAfInit(). Still mutates the live MAC
  extended address -- never run two units with the same IEEE on one network,
  and treat this as freshly re-verified rather than long-proven.
"""

import argparse
import json
import struct
import sys
import time

try:
    import serial  # pyserial
except ImportError:  # pragma: no cover
    sys.exit("This tool needs pyserial: pip install pyserial")

# --- SerialLink framing (SerialLink.c) --------------------------------------
SL_START, SL_ESC, SL_END = 0x01, 0x02, 0x03

# --- Message types ----------------------------------------------------------
MSG_STATUS = 0x8000
MSG_CAP_REQ, MSG_CAP_RSP = 0x0D0F, 0x8D0F
# Typed OCB metadata (identity) — custom_diag.c
MSG_OCB_BEGIN_REQ, MSG_OCB_BEGIN_RSP = 0x0D18, 0x8D18
MSG_OCB_CORE_REQ, MSG_OCB_CORE_RSP = 0x0D19, 0x8D19
MSG_OCB_END_REQ, MSG_OCB_END_RSP = 0x0D1B, 0x8D1B
# Experimental key export / restore — ocb_experimental.c
MSG_CHALLENGE_REQ, MSG_CHALLENGE_RSP = 0x0D20, 0x8D20
MSG_UNLOCK_REQ, MSG_UNLOCK_RSP = 0x0D21, 0x8D21
MSG_SECRET_CORE_REQ, MSG_SECRET_CORE_RSP = 0x0D22, 0x8D22
MSG_LINK_KEY_REQ, MSG_LINK_KEY_RSP = 0x0D23, 0x8D23
MSG_RESTORE_BEGIN_REQ, MSG_RESTORE_BEGIN_RSP = 0x0D24, 0x8D24
MSG_RESTORE_FIELD_REQ, MSG_RESTORE_FIELD_RSP = 0x0D25, 0x8D25
MSG_RESTORE_LINK_REQ, MSG_RESTORE_LINK_RSP = 0x0D26, 0x8D26
MSG_VALIDATE_REQ, MSG_VALIDATE_RSP = 0x0D27, 0x8D27
MSG_COMMIT_REQ, MSG_COMMIT_RSP = 0x0D28, 0x8D28

ABI, SCHEMA = 1, 1
CONFIRM_MAGIC = 0x4F434221  # "OCB!" — public, not a secret

# Restore field ids (ocb_experimental.h)
F_NWK_KEY, F_NWK_KEY_SEQ, F_NWK_OUT_FC = 0x0001, 0x0002, 0x0003
F_PAN_ID, F_EXT_PAN_ID, F_CHANNEL = 0x0004, 0x0005, 0x0006
F_NWK_ADDR, F_NWK_UPDATE_ID = 0x0007, 0x0008
F_TC_ADDR, F_TC_LINK_KEY, F_TC_KEY_TYPE = 0x0009, 0x000A, 0x000B
F_ADOPT_IEEE = 0x000C

# Export link-key kinds (docs/OCB_UART_ABI.md)
KIND_DEFAULT_TC, KIND_APS_TABLE, KIND_FLASH_TCLK = 0, 1, 2
FLASH_TCLK_ENTRIES = 70
AVAIL_TC_LINK_KEY, AVAIL_EUI = (1 << 2), (1 << 5)


class SerialLink:
    """Minimal ZiGate SerialLink transport."""

    # 15s default: generous headroom for a slow link; restoring the NWK
    # outgoing frame counter (field 0x0003) used to loop PDM_eIncrementBitmap()
    # once per 1024 counter units on-device, which could take many minutes for
    # a large counter -- fixed firmware-side by writing the target bitmap value
    # in one shot (ePDM_SetBitmapToValue()) instead of looping, so no single
    # RESTORE_FIELD round trip is meaningfully slower than the others anymore.
    def __init__(self, port, baud=115200, timeout=15.0):
        self.ser = serial.Serial(port, baud, timeout=0.05)
        self.timeout = timeout
        self._buf = bytearray()

    def close(self):
        self.ser.close()

    @staticmethod
    def _crc(msg_type, payload):
        crc = (msg_type >> 8) & 0xFF
        crc ^= msg_type & 0xFF
        crc ^= (len(payload) >> 8) & 0xFF
        crc ^= len(payload) & 0xFF
        for b in payload:
            crc ^= b
        return crc

    @staticmethod
    def _encode(value):
        out = bytearray()
        for b in value:
            if b < 0x10:
                out.append(SL_ESC)
                out.append(b ^ 0x10)
            else:
                out.append(b)
        return out

    def send(self, msg_type, payload=b""):
        payload = bytes(payload)
        frame = bytearray([SL_START])
        header = struct.pack(">HH", msg_type, len(payload))
        body = header + bytes([self._crc(msg_type, payload)]) + payload
        frame += self._encode(body)
        frame.append(SL_END)
        self.ser.write(frame)

    def _read_frame(self, deadline):
        """Read one raw (unescaped) frame body: type(2) len(2) crc(1) payload.

        START(0x01)/ESC(0x02)/END(0x03) never occur as escaped data, so frame
        boundaries can be located in the raw stream directly. self._buf is kept
        across calls (not a local variable reset per call) so any bytes trailing
        the frame just extracted -- e.g. a STATUS frame arriving in the same
        read() chunk as its immediately-following typed response, which the
        firmware always sends back to back -- aren't discarded. An earlier
        version used a per-call local buffer and silently dropped the second
        frame whenever both landed in one read(), which timed out on every
        single OCB command (each one gets a STATUS + typed response pair)."""
        while True:
            start = self._buf.find(bytes([SL_START]))
            if start == -1:
                self._buf.clear()
            else:
                end = self._buf.find(bytes([SL_END]), start + 1)
                if end != -1:
                    escaped = self._buf[start + 1:end]
                    del self._buf[:end + 1]
                    out = bytearray()
                    esc = False
                    for b in escaped:
                        if b == SL_ESC:
                            esc = True
                            continue
                        out.append(b ^ 0x10 if esc else b)
                        esc = False
                    return bytes(out)
                del self._buf[:start]
            if time.time() >= deadline:
                return None
            chunk = self.ser.read(64)
            if chunk:
                self._buf += chunk

    def recv(self, want_type, deadline=None):
        """Return payload bytes of the next frame matching want_type."""
        if deadline is None:
            deadline = time.time() + self.timeout
        while time.time() < deadline:
            raw = self._read_frame(deadline)
            if not raw or len(raw) < 5:
                continue
            mtype, mlen = struct.unpack(">HH", raw[0:4])
            payload = raw[5:5 + mlen]
            if mtype == want_type:
                return payload
            # otherwise ignore (status frames, logs, etc.)
        raise TimeoutError(f"no 0x{want_type:04X} within {self.timeout}s")

    def transact(self, req_type, rsp_type, payload=b""):
        self.send(req_type, payload)
        return self.recv(rsp_type)


def _prefix(txn):
    return struct.pack(">BBI", ABI, SCHEMA, txn)


class Ocb:
    """Experimental OCB export/restore session over a SerialLink."""

    def __init__(self, link):
        self.link = link
        self.txn = 0x4F434201

    def _next_txn(self):
        self.txn = (self.txn + 1) & 0xFFFFFFFF
        return self.txn

    # -- unlock -------------------------------------------------------------
    def unlock(self):
        txn = self._next_txn()
        rsp = self.link.transact(MSG_CHALLENGE_REQ, MSG_CHALLENGE_RSP, _prefix(txn))
        status = rsp[6]
        if status != 0:
            raise RuntimeError(f"CHALLENGE status {status}")
        (nonce,) = struct.unpack(">I", rsp[7:11])
        confirmation = (nonce ^ txn ^ CONFIRM_MAGIC) & 0xFFFFFFFF
        req = _prefix(txn) + struct.pack(">II", nonce, confirmation)
        rsp = self.link.transact(MSG_UNLOCK_REQ, MSG_UNLOCK_RSP, req)
        if rsp[6] != 0:
            raise RuntimeError(f"UNLOCK status {rsp[6]} (not unlocked)")
        return txn  # keep using this txn while unlocked

    # -- backup -------------------------------------------------------------
    def read_metadata(self):
        """Identity via the typed OCB metadata subset (0x0D18/0x0D19)."""
        txn = self._next_txn()
        begin = self.link.transact(MSG_OCB_BEGIN_REQ, MSG_OCB_BEGIN_RSP, _prefix(txn))
        # common rsp: abi,schema,txn(4),session(4),status(1) = 11
        (session,) = struct.unpack(">I", begin[6:10])
        status = begin[10]
        if status != 0:
            raise RuntimeError(f"OCB EXPORT_BEGIN status {status}")
        common = struct.pack(">BBII", ABI, SCHEMA, txn, session)
        core = self.link.transact(MSG_OCB_CORE_REQ, MSG_OCB_CORE_RSP, common)
        body = core[11:11 + 44]  # 44-byte CORE body
        (fields, coord_ieee, pan, ext_pan, channel, channel_mask,
         nwk_update_id, sec_level, nwk_key_seq, nwk_out, tc_ieee,
         aps_flags, aps_key_type) = struct.unpack(">IQHQBIBBBIQBB", body)
        self.link.send(MSG_OCB_END_REQ, common)  # best-effort close
        return dict(fields=fields, coordinator_ieee=coord_ieee, pan_id=pan,
                    extended_pan_id=ext_pan, channel=channel,
                    channel_mask=channel_mask, nwk_update_id=nwk_update_id,
                    security_level=sec_level, nwk_key_sequence=nwk_key_seq,
                    nwk_out_counter=nwk_out, tc_ieee=tc_ieee)

    def read_secret_core(self, txn):
        rsp = self.link.transact(MSG_SECRET_CORE_REQ, MSG_SECRET_CORE_RSP, _prefix(txn))
        if rsp[6] != 0:
            raise RuntimeError(f"SECRET_CORE status {rsp[6]}")
        (available, _limits, nwk_seq) = struct.unpack(">IIB", rsp[7:16])
        nwk_key = rsp[16:32]
        (nwk_out,) = struct.unpack(">I", rsp[32:36])
        tc_type = rsp[36]
        tc_key = rsp[37:53]
        (tc_out, tc_in) = struct.unpack(">II", rsp[53:61])
        return dict(available=available, nwk_seq=nwk_seq, nwk_key=nwk_key,
                    nwk_out=nwk_out, tc_type=tc_type, tc_key=tc_key,
                    tc_out=tc_out, tc_in=tc_in)

    def read_link_key(self, txn, kind, index):
        req = _prefix(txn) + struct.pack(">BB", kind, index)
        rsp = self.link.transact(MSG_LINK_KEY_REQ, MSG_LINK_KEY_RSP, req)
        status = rsp[6]
        if status != 0:
            return None  # NOT_FOUND etc.
        (r_kind, r_index) = struct.unpack(">BB", rsp[7:9])
        (eui,) = struct.unpack(">Q", rsp[9:17])
        (available,) = struct.unpack(">I", rsp[17:21])
        key_type = rsp[21]
        key = rsp[22:38]
        (aps_out, aps_in) = struct.unpack(">II", rsp[38:46])
        if not (available & AVAIL_TC_LINK_KEY) or not (available & AVAIL_EUI):
            return None
        return dict(eui=eui, key=key, key_type=key_type,
                    aps_out=aps_out, aps_in=aps_in)

    def backup(self):
        meta = self.read_metadata()
        txn = self.unlock()
        secret = self.read_secret_core(txn)
        devices = {}
        # Default TC key + every flash-TCLK slot.
        for kind, count in ((KIND_DEFAULT_TC, 1),
                            (KIND_APS_TABLE, 1),
                            (KIND_FLASH_TCLK, FLASH_TCLK_ENTRIES)):
            for idx in range(count):
                lk = self.read_link_key(txn, kind, idx)
                if lk and lk["eui"] not in (0, 0xFFFFFFFFFFFFFFFF):
                    devices[lk["eui"]] = lk
        return _to_json(meta, secret, list(devices.values()))

    # -- restore ------------------------------------------------------------
    def _field(self, txn, field_id, value):
        req = _prefix(txn) + struct.pack(">HH", field_id, len(value)) + bytes(value)
        rsp = self.link.transact(MSG_RESTORE_FIELD_REQ, MSG_RESTORE_FIELD_RSP, req)
        status, (fid,), result = rsp[6], struct.unpack(">H", rsp[7:9]), rsp[9]
        if status != 0:
            raise RuntimeError(f"RESTORE_FIELD 0x{field_id:04X} status {status}")
        if result == 1:
            print(f"  field 0x{field_id:04X}: skipped (unknown to firmware)")
        elif result != 0:
            raise RuntimeError(f"RESTORE_FIELD 0x{field_id:04X} result {result}")

    def restore(self, doc, adopt_ieee=False):
        txn = self.unlock()
        rsp = self.link.transact(MSG_RESTORE_BEGIN_REQ, MSG_RESTORE_BEGIN_RSP, _prefix(txn))
        if rsp[6] != 0:
            raise RuntimeError(f"RESTORE_BEGIN status {rsp[6]}")

        nk = doc["network_key"]
        self._field(txn, F_NWK_KEY, bytes.fromhex(nk["key"]))
        self._field(txn, F_NWK_KEY_SEQ, bytes([nk["sequence_number"] & 0xFF]))
        self._field(txn, F_NWK_OUT_FC, struct.pack(">I", nk["frame_counter"] & 0xFFFFFFFF))
        self._field(txn, F_PAN_ID, struct.pack(">H", int(doc["pan_id"], 16)))
        self._field(txn, F_EXT_PAN_ID, bytes.fromhex(doc["extended_pan_id"]))
        self._field(txn, F_CHANNEL, bytes([doc["channel"] & 0xFF]))
        self._field(txn, F_NWK_ADDR, struct.pack(">H", 0x0000))
        if doc.get("nwk_update_id") is not None:
            self._field(txn, F_NWK_UPDATE_ID, bytes([doc["nwk_update_id"] & 0xFF]))

        ss = doc.get("stack_specific", {}).get("zigate", {})
        if ss.get("tc_link_key"):
            self._field(txn, F_TC_ADDR, bytes.fromhex(doc["coordinator_ieee"]))
            self._field(txn, F_TC_LINK_KEY, bytes.fromhex(ss["tc_link_key"]))
            self._field(txn, F_TC_KEY_TYPE, bytes([ss.get("tc_key_type", 1) & 0xFF]))

        if adopt_ieee:
            self._field(txn, F_ADOPT_IEEE, bytes.fromhex(doc["coordinator_ieee"]))

        for dev in doc.get("devices", []):
            lk = dev.get("link_key")
            if not lk or not lk.get("key"):
                continue
            eui = bytes.fromhex(dev["ieee_address"])
            key_type = lk.get("key_type", 2)
            req = _prefix(txn) + eui + bytes([key_type & 0xFF]) + bytes.fromhex(lk["key"])
            rsp = self.link.transact(MSG_RESTORE_LINK_REQ, MSG_RESTORE_LINK_RSP, req)
            if rsp[6] != 0:
                raise RuntimeError(f"RESTORE_LINK status {rsp[6]}")
            result = rsp[15]
            if result != 0:
                print(f"  link {dev['ieee_address']}: result {result}")

        rsp = self.link.transact(MSG_VALIDATE_REQ, MSG_VALIDATE_RSP, _prefix(txn))
        (present,) = struct.unpack(">I", rsp[7:11])
        mandatory_ok = rsp[11]
        print(f"VALIDATE present=0x{present:08X} mandatory_ok={mandatory_ok}")
        if not mandatory_ok:
            raise RuntimeError("mandatory fields missing; aborting before COMMIT")

        rsp = self.link.transact(MSG_COMMIT_REQ, MSG_COMMIT_RSP, _prefix(txn))
        if rsp[6] != 0:
            raise RuntimeError(f"COMMIT status {rsp[6]}")
        print("COMMIT ok — coordinator is rebooting onto the restored network.")


def _hex(value, width):
    return f"{value:0{width}X}"


def _to_json(meta, secret, devices):
    doc = {
        "metadata": {
            "version": 1,
            "format": "zigpy/open-coordinator-backup",
            "source": "ocb_backup.py@zigate-jn5169",
        },
        "coordinator_ieee": _hex(meta["coordinator_ieee"], 16),
        "pan_id": _hex(meta["pan_id"], 4),
        "extended_pan_id": _hex(meta["extended_pan_id"], 16),
        "nwk_update_id": meta["nwk_update_id"],
        "security_level": meta["security_level"],
        "channel": meta["channel"],
        "channel_mask": [meta["channel"]],
        "network_key": {
            "key": secret["nwk_key"].hex().upper(),
            "sequence_number": secret["nwk_seq"],
            "frame_counter": secret["nwk_out"],
        },
        "stack_specific": {
            "zigate": {
                "tc_link_key": secret["tc_key"].hex().upper(),
                "tc_key_type": secret["tc_type"],
            }
        },
        "devices": [
            {
                "ieee_address": _hex(d["eui"], 16),
                "nwk_address": None,
                "link_key": {
                    "key": d["key"].hex().upper(),
                    "key_type": d["key_type"],
                    "rx_counter": d["aps_in"],
                    "tx_counter": d["aps_out"],
                },
            }
            for d in devices
        ],
    }
    return doc


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("action", choices=["backup", "restore"])
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", help="backup: JSON output path")
    ap.add_argument("--in", dest="infile", help="restore: JSON input path")
    ap.add_argument("--adopt-ieee", action="store_true",
                    help="restore: override target MAC IEEE with the source's "
                         "(risky; devices keep their addresses)")
    args = ap.parse_args()

    link = SerialLink(args.port, args.baud)
    ocb = Ocb(link)
    try:
        if args.action == "backup":
            doc = ocb.backup()
            text = json.dumps(doc, indent=2)
            if args.out:
                with open(args.out, "w") as fh:
                    fh.write(text)
                print(f"wrote {args.out} ({len(doc['devices'])} devices)")
            else:
                print(text)
        else:
            if not args.infile:
                ap.error("restore needs --in")
            with open(args.infile) as fh:
                doc = json.load(fh)
            if args.adopt_ieee:
                print("WARNING: adopting the source IEEE. Ensure the source "
                      "coordinator is OFF this network before powering the target.")
            ocb.restore(doc, adopt_ieee=args.adopt_ieee)
    finally:
        link.close()


if __name__ == "__main__":
    main()
