# Diagnostic ABI — negotiated coordinator manufacturer-code command

Status: **implemented in firmware C** against v2395 (the ControlBridge port
compiles and links — see `MIGRATION_STATUS.md`). This document is the normative
wire contract that the Go host (`adapter/zigate`, `zigbee/manager`) codes
against, and that the firmware implements. The custom diagnostic protocol is at
**proto 1.2 / build rev 4** (`custom_diag.h`).

## Rationale

OpenLumi's ZiGate mutates the coordinator Node Descriptor manufacturer code
**implicitly, inside the device-join event handler** (the "Xiaomi/Lumi
workaround"). That is a hidden global side effect with no host visibility, no
validation, and no way to restore the default. It also races: the descriptor is
a single global resource, so overlapping joins/interviews cross-contaminate.

This ABI replaces that hidden mutation with an **explicit, negotiated,
host-driven command** with validation and readback. The firmware performs **no**
automatic manufacturer-code mutation on join.

## Honesty constraint (global descriptor, no per-device scoping)

The JN5169 coordinator has exactly **one** Node Descriptor. This command
rewrites that **global** descriptor. It **cannot** scope the manufacturer code
to a single peer device. The response therefore reports the *effective global*
code; any per-device isolation must be arranged by the host by serialising use
of the coordinator (see the Go-side lease in `zigbee/manufcode_lease.go`). The
firmware must not claim, and the host must not assume, per-device scoping.

## Capability negotiation

- Capability bit: **`1 << 10`** (`0x0000000000000400`) in the `0x8D0F`
  capability bitmap.
- The bit is set **only when the firmware actually implements this command**.
  Stock ZiGate and any build without the command leave it clear, and answer the
  request below with the generic `0x8000` status `2` ("unhandled"), which the
  host maps to `zigbee.ErrUnsupported` (backward compatible).
- The host must negotiate `0x0D0F` first and must not issue this command unless
  the bit is advertised.

## Command

| Name                                   | ID       |
|----------------------------------------|----------|
| `E_SL_MSG_MANUFACTURER_CODE_REQ`       | `0x0D16` |
| `E_SL_MSG_MANUFACTURER_CODE_RSP`       | `0x8D16` |

(`0x0D16`/`0x8D16` are otherwise unused in the reserved custom range; see
`SerialLink.h` allocations `0x0D0F`, `0x0D12`–`0x0D15`, `0x0D1F`. `0x0D00` is
**reserved/removed** — it was the TCLK diagnostic feature, deleted as
security-sensitive; the request ID now replies `0x8000` status "unhandled".)

### Request payload (application bytes, big-endian)

```
off size field
 0   1   version        = 0x01
 1   1   op             0x00 GET, 0x01 SET, 0x02 RESTORE_DEFAULT
 2   2   code (uint16)  desired code; used for SET only, else 0x0000
```

### Response payload (application bytes; firmware then appends its LQI byte)

```
off size field
 0   1   version        = 0x01
 1   1   status         0x00 OK
                        0x01 INVALID (validation/readback failed)
                        0x02 UNSUPPORTED_OP
 2   2   effective code (uint16)  code now in the GLOBAL Node Descriptor (readback)
 4   2   default code   (uint16)  firmware baked-in default (RESTORE target)
```

The trailing LQI byte that `vSL_WriteMessage` appends is **not** part of the
application payload; the host strips it (as with every other custom response).

### Semantics

- **SET**: validate `code` (any `uint16` is syntactically valid; the firmware
  may reject reserved values with `status=INVALID`), write the global Node
  Descriptor, **re-read** it, and return the re-read value as `effective code`.
  The host treats the operation as successful only when
  `status==OK && effective==requested`. This is the validation + readback
  requirement.
- **GET**: return the current global `effective code` without modifying it.
- **RESTORE_DEFAULT**: set the descriptor back to `default code` and return it as
  `effective`. The host restores via this op (or `SET default`); either way the
  host can always return the coordinator to its shipped default.
- The firmware performs no manufacturer-code change except in response to this
  command. There is no join-time mutation.

## Backward compatibility

- Firmware without the capability bit answers `0x0D16` with `0x8000` status `2`;
  the host returns `zigbee.ErrUnsupported`. No behavioural change for stock.
- Adding the command does not alter any existing command, capability bit, or the
  `0x8D0F` response layout (the bit was previously reserved/clear).

## Determinism / testability

The command is a pure request/response with an echoed `version`, a `status`, and
a readback field, so the host can assert exact bytes in a scripted NCP without
hardware. See `adapter/zigate/manufcode_test.go` on the host side.
