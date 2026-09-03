#!/usr/bin/env python3
"""Self-contained (stdlib-only) OCB client for running ON the OpenWRT gateway.

Drives /dev/ttymxc1 directly via termios (no pyserial). Verifies the firmware
booted, reads a backup, and can restore one. Same wire ABI as scripts/ocb_backup.py.

    check:   ocb_gw.py check   --port /dev/ttymxc1
    backup:  ocb_gw.py backup  --port /dev/ttymxc1 --out /tmp/net.json
    restore: ocb_gw.py restore --port /dev/ttymxc1 --in  /tmp/net.json [--adopt-ieee]

Note on --adopt-ieee: overrides the target coordinator's MAC IEEE with the
source's, so devices keep their bindings/addresses after a physical swap.
HIL testing (2026-08-31) found the firmware's
ZPS_vSetOverrideLocalIeeeAddr() call reliably hung boot when applied before
ZPS_eAplAfInit(); root-caused by disassembling libZPSMAC_Mini_SOC_JN516x.a /
libMiniMac_JN5169.a to a hardware MAC register write reached too early. Fixed
by moving that boot-time call to run after ZPS_eAplAfInit() instead. Still
mutates the live MAC extended address -- never run two units with the same
IEEE on one network, and re-verify on hardware after any firmware change to
this path.
"""
import argparse
import json
import os
import select
import struct
import sys
import termios
import time

SL_START, SL_ESC, SL_END = 0x01, 0x02, 0x03

MSG_GET_VERSION, MSG_VERSION_LIST = 0x0010, 0x8010
MSG_CAP_REQ, MSG_CAP_RSP = 0x0D0F, 0x8D0F
MSG_OCB_BEGIN_REQ, MSG_OCB_BEGIN_RSP = 0x0D18, 0x8D18
MSG_OCB_CORE_REQ, MSG_OCB_CORE_RSP = 0x0D19, 0x8D19
MSG_OCB_END_REQ = 0x0D1B
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
CONFIRM_MAGIC = 0x4F434221
CAP_BIT_OCB_META = 1 << 15
CAP_BIT_OCB_EXP = 1 << 16
CAP_BIT_BACKUP_QUALIFIED = 1 << 17

F_NWK_KEY, F_NWK_KEY_SEQ, F_NWK_OUT_FC = 0x0001, 0x0002, 0x0003
F_PAN_ID, F_EXT_PAN_ID, F_CHANNEL = 0x0004, 0x0005, 0x0006
F_NWK_ADDR, F_NWK_UPDATE_ID = 0x0007, 0x0008
F_TC_ADDR, F_TC_LINK_KEY, F_TC_KEY_TYPE = 0x0009, 0x000A, 0x000B
F_ADOPT_IEEE = 0x000C

KIND_DEFAULT_TC, KIND_APS_TABLE, KIND_FLASH_TCLK = 0, 1, 2
FLASH_TCLK_ENTRIES = 70
AVAIL_TC_LINK_KEY, AVAIL_EUI = (1 << 2), (1 << 5)


class RawSerial:
    # 15s default: generous headroom for a slow link; restoring the NWK
    # outgoing frame counter (field 0x0003) used to loop PDM_eIncrementBitmap()
    # once per 1024 counter units on-device, which could take many minutes for
    # a large counter -- fixed firmware-side by writing the target bitmap value
    # in one shot (ePDM_SetBitmapToValue()) instead of looping, so no single
    # RESTORE_FIELD round trip is meaningfully slower than the others anymore.
    def __init__(self, port, baud=115200, timeout=15.0):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
        speed = getattr(termios, "B%d" % baud)
        a = termios.tcgetattr(self.fd)
        a[0] = 0                                             # iflag
        a[1] = 0                                             # oflag
        a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # cflag 8N1
        a[3] = 0                                             # lflag (raw)
        a[4] = speed                                         # ispeed
        a[5] = speed                                         # ospeed
        a[6][termios.VMIN] = 0
        a[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, a)
        termios.tcflush(self.fd, termios.TCIOFLUSH)
        self.timeout = timeout

    def write(self, data):
        os.write(self.fd, bytes(data))

    def read(self, n):
        r, _, _ = select.select([self.fd], [], [], 0.05)
        if not r:
            return b""
        try:
            return os.read(self.fd, n)
        except OSError:
            return b""

    def close(self):
        os.close(self.fd)


class Link:
    def __init__(self, ser):
        self.ser = ser
        self.timeout = ser.timeout
        self._buf = bytearray()   # persistent stream buffer (keeps bytes past END)

    @staticmethod
    def _crc(mtype, payload):
        crc = (mtype >> 8) & 0xFF
        crc ^= mtype & 0xFF
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
                out += bytes([SL_ESC, b ^ 0x10])
            else:
                out.append(b)
        return out

    def send(self, mtype, payload=b""):
        payload = bytes(payload)
        body = struct.pack(">HH", mtype, len(payload)) + bytes([self._crc(mtype, payload)]) + payload
        frame = bytearray([SL_START]) + self._encode(body) + bytes([SL_END])
        self.ser.write(frame)

    def _read_frame(self, deadline):
        # START(0x01)/ESC(0x02)/END(0x03) never occur as escaped data, so we can
        # locate frame boundaries in the raw stream and keep any trailing bytes
        # (e.g. a STATUS frame followed immediately by its typed response).
        while True:
            while self._buf and self._buf[0] != SL_START:
                del self._buf[0]
            if self._buf and self._buf[0] == SL_START:
                end = self._buf.find(SL_END, 1)
                if end != -1:
                    escaped = self._buf[1:end]
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
            if time.time() >= deadline:
                return None
            chunk = self.ser.read(256)
            if chunk:
                self._buf += chunk

    def recv(self, want, deadline=None):
        if deadline is None:
            deadline = time.time() + self.timeout
        while time.time() < deadline:
            raw = self._read_frame(deadline)
            if not raw or len(raw) < 5:
                continue
            mtype, mlen = struct.unpack(">HH", raw[0:4])
            if mtype == want:
                return raw[5:5 + mlen]
        raise TimeoutError("no 0x%04X within %.1fs" % (want, self.timeout))

    def transact(self, req, rsp, payload=b""):
        self.send(req, payload)
        return self.recv(rsp)

    def flush(self):
        """Drop buffered + in-flight bytes so the next exchange starts clean."""
        self._buf.clear()
        end = time.time() + 0.3
        while time.time() < end:
            if not self.ser.read(256):
                break


def _prefix(txn):
    return struct.pack(">BBI", ABI, SCHEMA, txn)


def _hx(v, w):
    return "%0*X" % (w, v)


class Ocb:
    def __init__(self, link):
        self.link = link
        self.txn = 0x4F434201

    def _txn(self):
        self.txn = (self.txn + 1) & 0xFFFFFFFF
        return self.txn

    # -- start / capability -------------------------------------------------
    def check(self):
        ver = self.link.transact(MSG_GET_VERSION, MSG_VERSION_LIST)
        (version,) = struct.unpack(">I", ver[0:4])
        nonce = 0x12345678
        req = bytes([0x5A, 0x47, 0x48, 0x58, 1, 2]) + struct.pack(">I", nonce)
        cap = self.link.transact(MSG_CAP_REQ, MSG_CAP_RSP, req)
        # magic[4] nonce[4] pmaj[1] pmin[1] build_id[4] cap[8] maxpl[2]
        (r_nonce,) = struct.unpack(">I", cap[4:8])
        pmaj, pmin = cap[8], cap[9]
        (build_id,) = struct.unpack(">I", cap[10:14])
        (caps,) = struct.unpack(">Q", cap[14:22])
        (maxpl,) = struct.unpack(">H", cap[22:24])
        print("firmware VERSION      = 0x%08X" % version)
        print("diag proto            = %d.%d" % (pmaj, pmin))
        print("build_id              = 0x%08X (expect 0x0100C525 for experimental)" % build_id)
        print("capabilities          = 0x%016X" % caps)
        print("  OCB metadata (b15)  = %s" % bool(caps & CAP_BIT_OCB_META))
        print("  OCB exp keys/restore(b16) = %s" % bool(caps & CAP_BIT_OCB_EXP))
        print("  BackupCapable (b17) = %s (must be False)" % bool(caps & CAP_BIT_BACKUP_QUALIFIED))
        print("nonce echo ok         = %s ; max_payload=%d" % (r_nonce == nonce, maxpl))
        ok = (caps & CAP_BIT_OCB_EXP) and not (caps & CAP_BIT_BACKUP_QUALIFIED)
        return ok, build_id, caps

    def unlock(self):
        self.link.flush()
        txn = self._txn()
        rsp = self.link.transact(MSG_CHALLENGE_REQ, MSG_CHALLENGE_RSP, _prefix(txn))
        if rsp[6] != 0:
            raise RuntimeError("CHALLENGE status %d" % rsp[6])
        (nonce,) = struct.unpack(">I", rsp[7:11])
        conf = (nonce ^ txn ^ CONFIRM_MAGIC) & 0xFFFFFFFF
        rsp = self.link.transact(MSG_UNLOCK_REQ, MSG_UNLOCK_RSP,
                                 _prefix(txn) + struct.pack(">II", nonce, conf))
        if rsp[6] != 0:
            raise RuntimeError("UNLOCK status %d (not unlocked)" % rsp[6])
        return txn

    def read_metadata(self):
        txn = self._txn()
        b = self.link.transact(MSG_OCB_BEGIN_REQ, MSG_OCB_BEGIN_RSP, _prefix(txn))
        (session,) = struct.unpack(">I", b[6:10])
        if b[10] != 0:
            raise RuntimeError("OCB EXPORT_BEGIN status %d" % b[10])
        common = struct.pack(">BBII", ABI, SCHEMA, txn, session)
        core = self.link.transact(MSG_OCB_CORE_REQ, MSG_OCB_CORE_RSP, common)
        body = core[11:11 + 44]
        (fields, coord, pan, extpan, ch, chmask, upd, sec, kseq, nout, tc,
         aflags, aktype) = struct.unpack(">IQHQBIBBBIQBB", body)
        self.link.send(MSG_OCB_END_REQ, common)
        return dict(coordinator_ieee=coord, pan_id=pan, extended_pan_id=extpan,
                    channel=ch, nwk_update_id=upd, security_level=sec,
                    tc_ieee=tc)

    def read_secret(self, txn):
        r = self.link.transact(MSG_SECRET_CORE_REQ, MSG_SECRET_CORE_RSP, _prefix(txn))
        if r[6] != 0:
            raise RuntimeError("SECRET_CORE status %d" % r[6])
        (available, _lim, seq) = struct.unpack(">IIB", r[7:16])
        return dict(available=available, seq=seq, nwk_key=r[16:32],
                    nwk_out=struct.unpack(">I", r[32:36])[0], tc_type=r[36],
                    tc_key=r[37:53],
                    tc_out=struct.unpack(">I", r[53:57])[0],
                    tc_in=struct.unpack(">I", r[57:61])[0])

    def read_link(self, txn, kind, idx):
        r = self.link.transact(MSG_LINK_KEY_REQ, MSG_LINK_KEY_RSP,
                               _prefix(txn) + struct.pack(">BB", kind, idx))
        if r[6] != 0:
            return None
        (eui,) = struct.unpack(">Q", r[9:17])
        (avail,) = struct.unpack(">I", r[17:21])
        if not (avail & AVAIL_TC_LINK_KEY) or not (avail & AVAIL_EUI):
            return None
        if eui in (0, 0xFFFFFFFFFFFFFFFF):
            return None
        return dict(eui=eui, key=r[22:38], key_type=r[21],
                    aps_out=struct.unpack(">I", r[38:42])[0],
                    aps_in=struct.unpack(">I", r[42:46])[0])

    def backup(self):
        meta = self.read_metadata()
        txn = self.unlock()
        sec = self.read_secret(txn)
        devs = {}
        for kind, cnt in ((KIND_DEFAULT_TC, 1), (KIND_APS_TABLE, 1),
                          (KIND_FLASH_TCLK, FLASH_TCLK_ENTRIES)):
            for i in range(cnt):
                lk = self.read_link(txn, kind, i)
                if lk:
                    devs[lk["eui"]] = lk
        doc = {
            "metadata": {"version": 1, "format": "zigpy/open-coordinator-backup",
                         "source": "ocb_gw.py@zigate-jn5169"},
            "coordinator_ieee": _hx(meta["coordinator_ieee"], 16),
            "pan_id": _hx(meta["pan_id"], 4),
            "extended_pan_id": _hx(meta["extended_pan_id"], 16),
            "nwk_update_id": meta["nwk_update_id"],
            "security_level": meta["security_level"],
            "channel": meta["channel"], "channel_mask": [meta["channel"]],
            "network_key": {"key": sec["nwk_key"].hex().upper(),
                            "sequence_number": sec["seq"],
                            "frame_counter": sec["nwk_out"]},
            "stack_specific": {"zigate": {"tc_link_key": sec["tc_key"].hex().upper(),
                                          "tc_key_type": sec["tc_type"]}},
            "devices": [{"ieee_address": _hx(d["eui"], 16), "nwk_address": None,
                         "link_key": {"key": d["key"].hex().upper(),
                                      "key_type": d["key_type"],
                                      "rx_counter": d["aps_in"],
                                      "tx_counter": d["aps_out"]}}
                        for d in devs.values()],
        }
        return doc

    # -- restore ------------------------------------------------------------
    def _field(self, txn, fid, value):
        r = self.link.transact(MSG_RESTORE_FIELD_REQ, MSG_RESTORE_FIELD_RSP,
                               _prefix(txn) + struct.pack(">HH", fid, len(value)) + bytes(value))
        if r[6] != 0:
            raise RuntimeError("RESTORE_FIELD 0x%04X status %d" % (fid, r[6]))
        res = r[9]
        tag = {0: "applied", 1: "SKIPPED(unknown)", 2: "BAD_LEN", 3: "unavailable"}.get(res, res)
        print("  field 0x%04X -> %s" % (fid, tag))
        return res

    def restore(self, doc, adopt=False):
        txn = self.unlock()
        r = self.link.transact(MSG_RESTORE_BEGIN_REQ, MSG_RESTORE_BEGIN_RSP, _prefix(txn))
        if r[6] != 0:
            raise RuntimeError("RESTORE_BEGIN status %d" % r[6])
        (rcaps,) = struct.unpack(">I", r[7:11])
        print("RESTORE_BEGIN ok, restore_caps=0x%08X" % rcaps)
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
        if adopt:
            self._field(txn, F_ADOPT_IEEE, bytes.fromhex(doc["coordinator_ieee"]))
        nlink = 0
        for dev in doc.get("devices", []):
            lk = dev.get("link_key")
            if not lk or not lk.get("key"):
                continue
            eui = bytes.fromhex(dev["ieee_address"])
            req = _prefix(txn) + eui + bytes([lk.get("key_type", 2) & 0xFF]) + bytes.fromhex(lk["key"])
            r = self.link.transact(MSG_RESTORE_LINK_REQ, MSG_RESTORE_LINK_RSP, req)
            if r[6] != 0:
                raise RuntimeError("RESTORE_LINK status %d" % r[6])
            nlink += 1 if r[15] == 0 else 0
        print("link keys applied     = %d" % nlink)
        r = self.link.transact(MSG_VALIDATE_REQ, MSG_VALIDATE_RSP, _prefix(txn))
        (present,) = struct.unpack(">I", r[7:11])
        print("VALIDATE present=0x%08X mandatory_ok=%d" % (present, r[11]))
        if not r[11]:
            raise RuntimeError("mandatory fields missing; NOT committing")
        r = self.link.transact(MSG_COMMIT_REQ, MSG_COMMIT_RSP, _prefix(txn))
        if r[6] != 0:
            raise RuntimeError("COMMIT status %d" % r[6])
        print("COMMIT ok -> coordinator rebooting onto the restored network.")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("action", choices=["check", "backup", "restore"])
    ap.add_argument("--port", default="/dev/ttymxc1")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out")
    ap.add_argument("--in", dest="infile")
    ap.add_argument("--adopt-ieee", action="store_true",
                    help="restore: override target MAC IEEE with the source's "
                         "(risky; never run two units with the same IEEE)")
    args = ap.parse_args()

    ser = RawSerial(args.port, args.baud)
    ocb = Ocb(Link(ser))
    try:
        if args.action == "check":
            ok, _, _ = ocb.check()
            print("RESULT:", "OK (experimental restore build running)" if ok else "UNEXPECTED")
            sys.exit(0 if ok else 1)
        elif args.action == "backup":
            doc = ocb.backup()
            text = json.dumps(doc, indent=2)
            if args.out:
                open(args.out, "w").write(text)
                print("wrote %s (%d devices)" % (args.out, len(doc["devices"])))
            else:
                print(text)
        else:
            doc = json.load(open(args.infile))
            if args.adopt_ieee:
                print("WARNING: adopting the source IEEE. Ensure the source "
                      "coordinator is OFF this network before powering the target.")
            ocb.restore(doc, adopt=args.adopt_ieee)
    finally:
        ser.close()


if __name__ == "__main__":
    main()
