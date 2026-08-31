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
and link-key **export and a streamed, field-tagged restore** (`0x0D24`..`0x0D28`)
with a 30-second nonce confirmation that is explicitly **not authentication**.
Restore is bench-only and unqualified: BackupCapable (reserved bit 17) stays
clear because the path is unauthenticated and non-atomic. HIL testing (real
JN5169, 2026-08-31) verified network key, PAN/ext-PAN id, channel, TC link
key, **the NWK outgoing frame counter, and coordinator-IEEE adoption** all
restore correctly and survive the COMMIT reboot, including onto a target whose
PDM had just been fully erased. Both the frame counter and IEEE adoption
needed non-obvious fixes found by disassembling the linked NXP SDK libraries
with `ba-elf-objdump` — the frame counter is persisted via an undocumented PDM
*bitmap* record rather than a plain value, and IEEE adoption's boot hook was
calling into a hardware MAC register write before the radio was initialised.
See [`docs/OCB_UART_ABI.md`](docs/OCB_UART_ABI.md) for the full traces. IEEE
adoption mutates the live MAC address and has so far only been HIL-verified as
a same-unit self-restore, not a true two-device migration — treat it as
freshly re-verified, not long-proven. Flash-TCLK APS counters aren't
restorable (restored keys re-sync their APS counter on the next exchange). See
[`docs/OCB_UART_ABI.md`](docs/OCB_UART_ABI.md) for full detail and HIL
evidence.

> Status: **rev17 is hardware-qualified for the RODRET reset defect and the
> previously validated network/TX-power paths, not for every optional
> capability.** It preserved the existing network and TX power `7`; two awake
> IKEA RODRET interviews completed Node Descriptor, Active Endpoints, Simple
> Descriptor and Basic reads without a reset. Typed OCB restore remains
> unimplemented and experimental key export remains unqualified. See
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

The custom diagnostic ABI is protocol **1.2**, build **rev 16**. Host-visible
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
framing and the trailing LQI byte streamed by `vSL_WriteMessage()`. Struct version is
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
| `0x0D20`…`0x0D2A` → `0x8D20`…`0x8D2A` | Default-off experimental trusted-UART key export and a streamed, field-tagged restore (`0x0D24`..`0x0D28`). Exact per-opcode layouts are in [`docs/OCB_UART_ABI.md`](docs/OCB_UART_ABI.md). |
| `0x0D2B` → `0x8D2B` | Capability-gated boot reset snapshot: empty req; rsp 6 (`version, status, flags, SYSCTRL-status:u16, exception_reason`). See [`docs/RESET_DIAGNOSTIC_ABI.md`](docs/RESET_DIAGNOSTIC_ABI.md). |
| `0x0D2C` → `0x8D2C` | Capability-gated retained exception context: empty req; rsp 22 (`version, status, reason, flags, SYSCTRL-status:u16, EPCR:u32, EEAR:u32, SP:u32, LR:u32`). |
| `0x0806` → `0x8806` | Legacy TX SET: req 1; successful rsp 2 (`six_bit_code, legacy_mapped_level`). Also emits outer `0x8000`; failed SET emits no `0x8806`. |
| `0x0807` → `0x8807` | Legacy TX GET: empty req; successful rsp 2 with the same representation, plus outer `0x8000`. |
| `0x0B00` → `0x8B00`, `0x0B01` → `0x8B01`, `0x0B02` | Unsafe legacy raw-PDM dump/restore/activate commands. Not dispatched by default; available only with `INSECURE_DEV_RAW_PDM=1` and `OCB_TYPED_SUPPORT=0`. |

Diagnostic capability bits are: groups bit 0, neighbours bit 1, routes bit 2,
GP commissioning bit 3 when compiled, TX power bit 9, manufacturer-code
control bit 10, general diagnostics bit 14, typed OCB metadata bit 15 when
compiled, experimental key export/restore bit 16 when compiled, and boot reset
summary diagnostics bit 18 and reset-context bit 19. Reserved
BackupCapable bit 17 is always clear. The wrapper's default GP + typed-OCB
build advertises `0x00000000000CC60F` and computes
`DIAG_FW_BUILD_ID=0x010DC53D`. Enabling experimental export adds bit 16,
yielding `0x00000000000DC60F` and build ID `0x010CC53D`. Bit 18 guarantees
only `0x0D2B`; bit 19 independently guarantees `0x0D2C`. These IDs identify
source constants, not hardware qualification.

Within the pre-existing diagnostic ABI, rev 9 changed no command encoding. It
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

Rev 9 also made the endpoint-1 **Time server (`0x000A`) read-only over
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

A successful legacy SET (`0x0806`) is persisted in the application PDM. Rev 10
observes every `ZPS_EVENT_NWK_STARTED` in `APP_vBdbCallback` before endpoint
routing, then defers application until `bdb_taskBDB()` has returned to the main
loop. This removes the rev9 callback-ordering assumption that failed physical
reset testing. The validated record is cached, so subsequent network starts do
not reread or write PDM. A new or changed record is read back and fully
validated before SET reports success. GET (`0x0807`) and both response
encodings are otherwise unchanged.
The record is explicitly packed and fixed at five bytes (`TX`, format version
2, code, CRC-8). Rev9's nominal five-byte C structure was actually eight bytes
under the BA2 ABI because of tail padding. Rev10 attempted to replace that
different-sized record at the same PDM ID, but physical HIL showed the EEPROM
PDM rejected the SET. Rev11 therefore stores v2 at `0x0012`; legacy ID `0x0011`
is ignored and permanently reserved. An upgraded device uses the default TX
power until the host performs one successful SET to create the v2 record.
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
# Default-off trusted-local-UART key export + streamed restore (bench-only).
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

The published rev9/db6f8f2 wrapper-default build reported
`text=255592 data=2104 bss=30421`, with 244 bytes between
`_minimum_heap_end=0x0400679c` and
`_stack_low_water_mark=0x04006890`:

```
BIN sha256 f17777bec16acd8f1586e56d5a3695f12c381603f634fee15f26859d7d1be6e0
ELF sha256 23d25e6b6968f2bcfa56393770e6184e9904d6ea6557e73484f8ae826ed378e1
```

Physical HIL disproved its documented TX persistence: SET 7 round-tripped
before adapter restart, but GET returned the default code 8 afterward. The
rev9 documentation therefore overclaimed persistence, and that hash must not
be cited as qualification for it.

The current rev10 replacement clean-build result is
`text=255696 data=2104 bss=30421`, with the same 244-byte linker margin:

```
BIN sha256 08224db07767e3409d6ea06c6cd8adf8774a9e861292fda0d350e930aae94f74
ELF sha256 e1f67c5e721f2f190e833af15f1d3ab69c07d427a4fb8923a8dccdd5589f930e
```

Two clean local builds produced those rev10 hashes. Physical HIL on a device
carrying rev9's old `0x0011` record showed that SET failed closed with status 1,
so this artifact is also not a valid persistence release. Rev11 abandons that
record ID and remains unapproved until its exact BIN is flashed and the same
SET/restart test passes.

The current rev11 clean-build result is:

```
text=255696 data=2104 bss=30421; linker RAM margin=244 bytes
BIN sha256 a704ba590c4a03e55be889d01c83b77c3491a09b708a8bca507b1829631b026e
ELF sha256 19d8192a4e98c35a0189cf8381b3c849d0bbd7a290202658b082090ce19b1bb6
```

Two clean local builds produced those rev11 hashes. This is build evidence,
not persistence HIL qualification.

Rev12 preserves the rev11 PDM `0x0012` record fix and adds the bounded
raw-ZDP, immutable SerialLink TX, strict raw-APS parser, and reset-diagnostic
changes described above. Two clean pinned-BA2 builds produced:

```
text=256032 data=2104 bss=30421; linker RAM margin=244 bytes
BIN size 258144 bytes
BIN sha256 38c3fbe2ccdfe2956130d01a2d470ab7a99424bfe5005f5304144a0f13c30a95
ELF sha256 125ab9c9eef30b6a74ab87512597a8fd953b0ce8ea2f5195aa8fca3f3c5d9c5f
DIAG_FW_BUILD_ID=0x0105C520
```

Rev12 was flashed and preserved the existing network, but TX persistence HIL
failed because the first GET after restart could precede the asynchronous
NWK_STARTED restore.

Rev13 schedules an initial restore immediately after `BDB_vStart()` while
retaining the later NWK_STARTED restore. Two clean pinned-BA2 builds produced:

```
text=256036 data=2104 bss=30421; linker RAM margin=244 bytes
BIN size 258148 bytes
BIN sha256 a510aaba81c320a26a7de76428e7231fc91466bddd990087c104c157a41ce96b
ELF sha256 ff3d310e64fa354f54fccf80a60007bbd13f6bb02dd27f6db07c4175abbc7dd5
DIAG_FW_BUILD_ID=0x0105C521
```

HIL of this exact rev13 BIN proved TX power 7 survives both a full power cycle
and a soft adapter restart. The same HIL exposed an unresolved software-reset
storm during an IKEA RODRET Active Endpoints interview.

Rev14 retains a guarded reset reason across software reset without writing PDM
or changing the six-byte `0x8D2B` response:

```
text=256308 data=2104 bss=30437; linker RAM margin=228 bytes
BIN size 258420 bytes
BIN sha256 5aeeaf1b91139bbae29f8f452db8f70fe921315cc378176610ec2027ecca6ab4
ELF sha256 3d98888f0f1e72e303ac5298efd002d24c2d2d26705cb2f78a5dfaeafffed79e
DIAG_FW_BUILD_ID=0x0105C522
```

Rev14 was flashed successfully. TX power 7 remained persistent, and RODRET
interview HIL identified the repeated resets as bus errors. It did not
localise or resolve the faulting instruction.

Rev14 HIL retained reason `1` across every reproduced RODRET interview reset,
proving the storm enters the JN5169 bus-error handler. Rev15 adds the fault
register context needed to map that exception to the exact ELF instruction:

```
text=256776 data=2104 bss=30469; linker RAM margin=196 bytes
BIN size 258888 bytes
BIN sha256 f052390fa76e520fbcc51866d3e11c9cee60f33d9f80c31dd7358c751ccbf073
ELF sha256 73d960a5820701638d7873ad0f2701365d816b41cb88a3b09ff1999b1d81d23e
DIAG_FW_BUILD_ID=0x0105C523
```

This is software/build evidence only; rev15 has not been flashed or
hardware-qualified.

Rev16 changes only capability negotiation and revision metadata: shipped bit
18 continues to guarantee `0x0D2B`, while new bit 19 independently guarantees
`0x0D2C`. Neither payload layout changed.

```
text=256780 data=2104 bss=30469; linker RAM margin=196 bytes
BIN size 258892 bytes
BIN sha256 4910a67aaeeb10fef7a5ad4e79065eeb2a1f3bc54677b342b94731ac525424fd
ELF sha256 3074798390dcd609502380b07970716c3d0f2ec3efa6f1fe671e734a11ce559d
DIAG_FW_BUILD_ID=0x010DC53C
```

Rev16 was flashed and reproduced the RODRET reset storm. Its retained context
localized every reset to a bus error in the v2395 ZPS
`bDuplicateCheck()` implementation.

Rev17 adds a narrow linker wrapper around `zps_psFindKeyDescr()`. Some stock
early failure paths leave its output index untouched when no key descriptor is
found, while `bDuplicateCheck()` still uses that value to address the incoming
frame-counter array. GCC had reused the same stack slot for the packet CRC,
producing an effectively random index. Rev17 sets the index to zero whenever
the lookup returns `NULL`; successful key lookup, key selection, and the replay
decision remain unchanged. On the keyless path, the duplicate-table snapshot
now records the valid slot-zero counter instead of reading an undefined
address.

Two clean pinned-toolchain builds produced the same BIN and ELF hashes:

```
text=256808 data=2104 bss=30469; linker RAM margin=196 bytes
BIN size 258920 bytes
BIN sha256 025e033cb73f8e68c57d41263c7b557888c49afa774c695aacce18dd053eecb4
ELF sha256 4c9fedc3c01412a68817b03587f38868b33aef7938df1df924f976f237936309
DIAG_FW_BUILD_ID=0x010DC53D
```

The shipped NOLTO ELF contains `__wrap_zps_psFindKeyDescr`, and the call in
`bDuplicateCheck()` targets that wrapper. The exact BIN above was flashed on a
ZiGate v1 JN5169. It preserved PAN `0x6ADA`, channel 15 and TX power `7`.
Two awake RODRET interviews received Node Descriptor `0x8002`, Active
Endpoints `0x8005`, Simple Descriptor `0x8004`, completed Basic reads, and
finished with one endpoint. No new `0x8006` restart indication appeared, and
the retained reset context stayed at the expected host-reset reason `0x10`.

## Provenance & licensing

NXP SDK files retain their original license headers. OpenLumi application code
and local changes remain identifiable through the preserved Git history. See
`LICENSES/`.

## Safety

This tree targets a coordinator that speaks a host serial protocol. Hardware
qualification is revision-specific: use the explicit HIL status beside each
artifact above, and do not treat a successful build as deployment validation.
