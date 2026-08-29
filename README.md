# ZiGate JN5169 coordinator firmware

Public, standalone firmware tree for a **ZiGate v1 (JN5169)** Zigbee
**coordinator**, built on the **NXP JN-SW-4170 v2395** SDK, carrying the
OpenLumi ZiGate v3.23 application and a versioned diagnostic and bounded
local-control protocol.

The firmware also contains a typed, snapshot-based OCB **metadata export**
subset (`0x0D18`..`0x0D1C`). It deliberately does not export keys, implement
restore, or claim BackupCapable. See
[`docs/OCB_UART_ABI.md`](docs/OCB_UART_ABI.md).

A separate default-off
`OCB_KEY_EXPORT_RESTORE_EXPERIMENTAL=1` build adds trusted-local-UART network
and link-key export with a 30-second nonce confirmation that is explicitly
**not authentication**. Restore remains blocked and BackupCapable remains
clear because v2395 does not expose flash-TCLK counters or atomic rollback.

> Status: **current publication candidate is not hardware-qualified.** Earlier
> pre-TX-persistence OCB candidates built reproducibly. The current source has
> now passed static/source checks and a clean pinned-BA2 default build, but no
> image has been flashed. Inspect and approve the exact BIN hash, then qualify
> reset, PDM, UART, network-restart, and security behavior on JN5169 hardware
> before publishing a flash image. See
> [`docs/MIGRATION_STATUS.md`](docs/MIGRATION_STATUS.md).

## Layout

```
<repo root>/          NXP JN-SW-4170 v2395 SDK (upstream git history preserved)
  Chip/ Components/ Platform/ Stack/ Tools/ build.txt
  app/                Application subtree (OpenLumi ZiGate ControlBridge)
    Source/{ZigbeeNodeControlBridge,Common}/
    Build/ZigbeeNodeControlBridge/Makefile
  scripts/build.sh    Reproducible build wrapper
  scripts/check.sh    Source and custom-ABI invariant checks
  docs/               PROVENANCE.md, MIGRATION_STATUS.md, DIAGNOSTIC_ABI_*.md
  LICENSES/           Licensing / provenance notices
```

See [`docs/PROVENANCE.md`](docs/PROVENANCE.md) for exact upstream commits,
hashes, and licensing boundaries.

The custom diagnostic ABI is protocol **1.2**, build **rev 9**. Host-visible
custom commands are capability-negotiated through `0x0D0F`/`0x8D0F`; see
[`docs/DIAGNOSTIC_ABI_MANUFACTURER_CODE.md`](docs/DIAGNOSTIC_ABI_MANUFACTURER_CODE.md)
and
[`docs/DIAGNOSTIC_ABI_GREEN_POWER.md`](docs/DIAGNOSTIC_ABI_GREEN_POWER.md),
which specifies the negotiated Green Power proxy commissioning command
`0x0D17`/`0x8D17` added in rev 7 and re-encoded in rev 8 with a per-request
32-bit transaction id (request 7 bytes, response 9 bytes) so a late response from a
timed-out request can never be mistaken for the answer to the next one. The
protocol version is unchanged at 1.2; every other command encoding is
unchanged.

### UART command inventory

All custom multi-byte fields are big-endian. Payload lengths exclude ZiGate
framing and the LQI byte appended by `vSL_WriteMessage()`. Struct version is
`1` unless stated otherwise.

| Request → response | Payload and availability |
|---|---|
| `0x0D00` → no `0x8D00` | Removed TCLK diagnostic. Any request receives only outer `0x8000` UNHANDLED_COMMAND. |
| `0x0D0F` → `0x8D0F` | Capability negotiation: req 10 (`"ZGHX", host_major, host_minor, nonce:u32`); rsp 24 (`"ZGHX", nonce, proto 1.2, build_id:u32, capabilities:u64, max_payload:u16`). |
| `0x0D12` → `0x8D12` | Local APS group operation: req 5; rsp 9. |
| `0x0D13` → `0x8D13` | Local APS group list: req 4; rsp `7 + rows`, each row `index:u8, group:u16, endpoint_count:u8, endpoints[]`, at most 5 rows and 16 endpoints per row. |
| `0x0D14` → `0x8D14` | Local neighbour page: req 4; rsp `7 + 23*n`, `n <= 8`. |
| `0x0D15` → `0x8D15` | Local route page: req 4; rsp `7 + 9*n`, `n <= 16`. |
| `0x0D16` → `0x8D16` | Global Node Descriptor manufacturer-code GET/SET/RESTORE_DEFAULT: req 4; rsp 6. Default is `0x1147`; not persisted. |
| `0x0D17` → `0x8D17` | GP proxy commissioning, only with `CLD_GREENPOWER`: req 7 and rsp 9 with echoed `transaction_id:u32`. |
| `0x0D18` → `0x8D18` | Typed OCB metadata EXPORT_BEGIN: req 6; rsp 19. Default-on with `OCB_TYPED_SUPPORT=1`. |
| `0x0D19` → `0x8D19` | Typed OCB EXPORT_CORE: req 10; rsp 55. |
| `0x0D1A` → `0x8D1A` | Typed OCB LINK_KEY_BY_EUI placeholder: req 18; rsp 24 with FIELD_UNAVAILABLE and key length zero. |
| `0x0D1B` → `0x8D1B` | Typed OCB EXPORT_END: req 10; rsp 16. |
| `0x0D1C` → `0x8D1C` | Typed OCB STATUS: req 10; rsp 20. |
| `0x0D1F` → `0x8D1F` | General diagnostics: empty request; 47-byte response. TCLK fields are `0xFF` with TCLK_UNAVAILABLE set. |
| `0x0D20`…`0x0D2A` → `0x8D20`…`0x8D2A` | Default-off experimental trusted-UART key export and explicit restore-unavailable stubs. Exact per-opcode layouts are in [`docs/OCB_UART_ABI.md`](docs/OCB_UART_ABI.md). |
| `0x0806` → `0x8806` | Legacy TX SET: req 1; successful rsp 2 (`six_bit_code, legacy_mapped_level`). Also emits outer `0x8000`; failed SET emits no `0x8806`. |
| `0x0807` → `0x8807` | Legacy TX GET: empty req; successful rsp 2 with the same representation, plus outer `0x8000`. |
| `0x0B00` → `0x8B00`, `0x0B01` → `0x8B01`, `0x0B02` | Unsafe legacy raw-PDM dump/restore/activate commands. Not dispatched by default; available only with `INSECURE_DEV_RAW_PDM=1` and `OCB_TYPED_SUPPORT=0`. |

Diagnostic capability bits are: groups bit 0, neighbours bit 1, routes bit 2,
GP commissioning bit 3 when compiled, TX power bit 9, manufacturer-code
control bit 10, general diagnostics bit 14, typed OCB metadata bit 15 when
compiled, and experimental key export bit 16 when compiled. Reserved
BackupCapable bit 17 is always clear. The wrapper's default GP + typed-OCB
build advertises `0x000000000000C60F` and computes
`DIAG_FW_BUILD_ID=0x0101C525`. Enabling experimental export adds bit 16,
yielding `0x000000000001C60F` and build ID `0x0100C525`. These IDs identify
source constants, not hardware qualification.

Within the pre-existing diagnostic ABI, rev 9 changes no command encoding. It
restores endpoint 1's **raw-NCP
transmit allowlist**: physical HIL showed the ZPS APS layer uses an endpoint's
`OutputClusters` list to authorize *host-originated raw `0x0530` transmissions*
— a Power Configuration `0x0001` read was rejected locally with APS `0xA3`
`ILLEGAL_REQUEST` while Basic `0x0000` succeeded only because Basic was listed.
In an NCP those entries describe what the host+firmware pair can originate, so
Power Configuration, Multistate Input, OTA, Thermostat UI, Illuminance Level
Sensing, Pressure, Occupancy and Electrical Measurement are listed again. This
is const/flash descriptor data only: no ZCL runtime instances were added and
`.data`/`.bss` are unchanged. See
[`docs/MIGRATION_STATUS.md`](docs/MIGRATION_STATUS.md).

rev 9 also makes the endpoint-1 **Time server (`0x000A`) read-only over
Zigbee**. The UTC value is host-owned: it is written only by
`E_SL_MSG_SET_TIMESERVER` (`0x0016`) and the firmware's 1 Hz increment, and
read by `E_SL_MSG_GET_TIMESERVER` (`0x0017`) and remote ZCL *reads*. Remote ZCL
*Write Attributes* to cluster `0x000A` are refused with ZCL status `0x7e`
NOT_AUTHORIZED **before** the attribute is modified, including in `RAW_MODE_ON`.
Without this, registering the Time server for network reads would also have let
any node holding only the network key rewrite the clock the host reads back —
the cluster's `E_ZCL_SECURITY_APPLINK` requirement is clamped to network-level
security by the ZCL core in this build. `.text` +60 B, `.data`/`.bss` unchanged.
Details and the machine-checked invariants are in
[`docs/MIGRATION_STATUS.md`](docs/MIGRATION_STATUS.md) ("Time cluster
ownership").

### TX-power fields: two intentionally different representations

`0x8D1F` (general diagnostics) reports the **canonical raw six-bit register
code** in both unsigned TX bytes. The legacy `0x8806`/`0x8807` frames keep
`byte0` = the same six-bit code but `byte1` = the **legacy mapped level**.
From rev 6 these two **do not agree, by design** — neither is changed for the
sake of matching the other. Key the `0x8D1F` interpretation off build
revision ≥ 6 via `DIAG_FW_BUILD_ID`.

A successful legacy SET (`0x0806`) is persisted in the application PDM. On
reset, power cycle, or in-process network restart/re-formation it is reapplied
whenever BDB forwards `ZPS_EVENT_NWK_STARTED` after its state machines have
consumed the event and the associated start/reset MLME operation is complete.
The validated record is cached, so subsequent network starts do not reread or
write PDM. GET (`0x0807`) and both response encodings are otherwise unchanged.
The accepted values remain exactly `0x00..0x0A` and `0x20..0x3F`. They are
native signed six-bit MiniMac codes (`0x20..0x3F` represent −32..−1), **not
calibrated dBm measurements**. Invalid, clamped, non-round-trippable, corrupt,
or unknown PDM record versions are never applied. Repeated SETs of the already
valid persisted code do not rewrite EEPROM.

## Target build cell

```
JN5169 / JN516x / COORDINATOR / BAUD=115200
GP_SUPPORT=1  LEGACY=1  R23_UPDATES=0  WWAH=0  OTA=0  TRACE=0
DEBUG=NONE  DISABLE_LTO=1  APP_AHI_CONTROL=1
INSECURE_DEV_RAW_PDM=0  OCB_TYPED_SUPPORT=1
OCB_KEY_EXPORT_RESTORE_EXPERIMENTAL=0
```

`OTA=0` is currently a legacy build-cell label, not a feature switch:
`zcl_options.h` still defines `CLD_OTA`/`OTA_SERVER`, the OTA sources are
compiled, and endpoint 1 advertises the OTA server. Do not treat this image as
OTA-disabled.

## Building

Requirements (host):

- BA2 toolchain (`ba-elf-gcc 4.7.4`, binutils 2.22). Not vendored — set
  `TOOLCHAIN_ROOT` to the directory containing `ba-elf-ba2/bin/`.
- `python3` with `xmltodict==0.13.0` and `lxml` for the SDK config generators.
  A repo-local venv is used automatically if present:

  ```sh
  python3 -m venv .venv
  .venv/bin/pip install "xmltodict==0.13.0" lxml
  ```

Then:

```sh
# defaults to the target build cell above
TOOLCHAIN_ROOT=/path/to/toolchain sh scripts/build.sh clean
TOOLCHAIN_ROOT=/path/to/toolchain sh scripts/build.sh all
```

Every build variable can be overridden from the environment (e.g.
`GP_SUPPORT=0 BAUD=1000000 sh scripts/build.sh all`).

Relevant non-default builds are explicit:

```sh
# Default-off trusted-local-UART key export; restore still returns unsupported.
OCB_KEY_EXPORT_RESTORE_EXPERIMENTAL=1 sh scripts/build.sh all

# No typed OCB commands or OCB capability bits.
OCB_TYPED_SUPPORT=0 sh scripts/build.sh all

# Insecure bench-only legacy raw-PDM commands; incompatible with typed OCB.
OCB_TYPED_SUPPORT=0 INSECURE_DEV_RAW_PDM=1 sh scripts/build.sh all
```

The Makefile rejects experimental OCB without typed OCB and rejects raw PDM
with either typed or experimental OCB.

Reproducibility: the wrapper pins `LC_ALL=C`, `TZ=UTC`, and
`SOURCE_DATE_EPOCH`.

No CI workflow is committed in this repository. Run `scripts/check.sh` and the
clean build locally before release. Only the flashable BIN should be required
to be byte-identical across build hosts; ELF debug metadata can be
host-dependent.

The current clean wrapper-default build reports
`text=255592 data=2104 bss=30421`, with 244 bytes between
`_minimum_heap_end=0x0400679c` and
`_stack_low_water_mark=0x04006890`. Its unapproved artifacts are:

```
BIN sha256 f17777bec16acd8f1586e56d5a3695f12c381603f634fee15f26859d7d1be6e0
ELF sha256 23d25e6b6968f2bcfa56393770e6184e9904d6ea6557e73484f8ae826ed378e1
```

An initial build and a subsequent clean regeneration produced those same
hashes. This is build evidence, not hardware qualification or flash approval.

## Provenance & licensing

NXP SDK files retain their original license headers. OpenLumi application code
and local changes remain identifiable through the preserved Git history. See
`LICENSES/`.

## Safety

This tree targets a coordinator that speaks a host serial protocol. Firmware
changes here are **not** hardware-validated; no device was flashed. Validate
on hardware before deployment.
