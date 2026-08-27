# Migration status — OpenLumi ZiGate v3.23 app → JN-SW-4170 v2395 SDK

Target build cell (the requested configuration):

```
JN5169 / JN516x / COORDINATOR / BAUD=115200
GP_SUPPORT=1  LEGACY=1  R23_UPDATES=0  WWAH=0  OTA=0  TRACE=0  DEBUG=NONE  DISABLE_LTO=1
```

Build entry point: `scripts/build.sh {clean|all}` (see header for overrides).

## Current milestone — reproducible linking image achieved

The requested image **compiles and links**:
`ZigbeeNodeControlBridge_JN5169_GP_Proxy_COORDINATOR_115200.{elf,bin}`
(JN5169 / COORDINATOR / 115200 / GP_SUPPORT=1 / LEGACY=1 / R23_UPDATES=0 /
WWAH=0). Two clean builds are **byte-identical** (`.bin` and `.elf`):

```
bin sha256 e4f007969e248f04cd8b49694ed3019a00208c07ca2fa51aad1444af20d7a329
elf sha256 d903dc164dc6283b1dec9c751b8e58e725aaf4fe712e607e68942483eb2fcfda
text=252808  data=2032  bss=30157
RAM headroom = 0x7fb4 - (_minimum_heap_end 0x664c + __stack_size 0x1770)
             = 32692 - 32188 = 504 bytes
```

(Hashes above are for **proto 1.2 / build rev 4** after the independent-review
fixes below. The pre-review rev4 image was
`bin 0f314f98… / elf 4b5ef213…`; restoring the `E_SL_MSG_SET_EXT_PANID` case
label re-included previously dead-code-eliminated handler code, +196 B text.
The earlier rev3 image was `bin 7a878ca6… / elf 872b6d5d…`.)

### Independent-review fixes (applied before final build)

1. **`case E_SL_MSG_SET_EXT_PANID` restored** in `app_Znc_cmds.c`. The manuf-code
   case insertion had dropped the label, orphaning the ExtPANID handler
   (`ZPS_eAplAibSetApsUseExtendedPanId`) as unreachable dead code so
   `0x0020` silently fell through to default. The label is restored; the block
   is reachable again.
2. **Manufacturer RESTORE_DEFAULT / default field** now uses the **actual
   shipped Node-Descriptor default 0x1147** (`.zpscfg`
   `ManufacturerCode="4423"`), captured by snapshotting the live descriptor
   **once before any SET** (single source of truth), not the wrong
   `ZCL_MANUFACTURER_CODE` (0x1037). `DIAG_MANUF_CODE_SHIPPED_DEFAULT` (0x1147)
   is the documented fallback. Docs updated.
3. **Neighbour IEEE bound** now uses `psNib->sTblSize.u16MacAddTableSize` (the
   MAC address-table size that `u16Lookup` / `ZPS_u64NwkNibGetMappedIeeeAddr`
   index) instead of the smaller, unrelated `u16AddrMap` (nwkAddressMap size),
   so valid mapped IEEE addresses at lookup indices ≥ `u16AddrMap` are no longer
   wrongly reported as NA. Verified against the SDK's own use in
   `appZpsExtendedDebug.c`.

Determinism is pinned by `scripts/build.sh` (`LC_ALL=C`, `TZ=UTC`,
`SOURCE_DATE_EPOCH`). Both Blocker A and Blocker B are resolved; the validated
safety fixes, the custom diagnostic ABI and the negotiated manufacturer-code
command are implemented (see below).

### RAM budget note (JN5169 32 KB SRAM)

The stock GP-proxy coordinator `.zpscfg` shipped `RoutingTableSize="255"`
(a 3060-byte route table) which overflowed SRAM by 1428 B at link
(`ASSERT ... "Possible overflow of RAM"`). It was right-sized to `70` to match
the four sibling coordinator `.zpscfg` variants in this tree, recovering
2208 B of BSS. Current headroom is small (**504 B**); new features that add BSS
must be checked against the link-time RAM assert.

### Green Power RAM reduction — evaluated, capacities preserved

The correction asked to reduce GP RAM per OpenLumi master `a0beb6f` "Reduce
Green Power coordinator RAM usage" **if safely possible, without silently
lowering documented capacities**. Findings:

- This target builds as `GP_PROXY_BASIC_DEVICE` (not `GP_COMBO_BASIC_DEVICE`),
  so the 1320-byte translation table (`asGpTranslationTable[5]`, guarded by
  `#ifdef GP_COMBO_BASIC_DEVICE`) is **already excluded** from this image.
- The remaining GP RAM is the combined proxy/sink table
  `sGPDeviceInfo…asZgpsSinkProxyTable[GP_NUMBER_OF_PROXY_SINK_TABLE_ENTRIES=5]`
  and the generated GP security table / TX queue (`.zpscfg`
  `GreenPowerSecurityTable Size="5"`, `GreenPowerTxQueue Size="5"`). A closer
  audit confirms **every** GP sizing macro is at its SDK default and is **not**
  overridden anywhere in this tree: proxy/sink=5, duplicate-filter=5,
  buffered-records=4, paired-endpoints=5, sink-group-list=2. None are inflated,
  so there is no oversized allocation to trim. These are the documented
  operational capacities (5 proxied GPDs / 5 security entries / 5 queued
  frames); reducing them lowers documented capacity, which the correction
  forbids.
- The `a0beb6f` diff cannot be fetched (no network access) to reproduce a
  capacity-neutral reduction verbatim, and inventing table shrinks would breach
  the "do not silently lower documented capacities" constraint.
- **Net:** GP capacities are left intact. RAM/flash was instead reclaimed by
  removing the security-sensitive TCLK 0x0D00 subsystem (−68 B `.data`,
  −804 B text). This item is documented as deliberately deferred rather than
  applied blind.

## What works today (baseline established)

1. **Repository structure** — v2395 SDK at the repo root with upstream history
   preserved and the application isolated under `app/`.
2. **Toolchain integration** — BA2 `ba-elf-gcc 4.7.4` compiles and assembles
   (verified on `irq_JN516x.S`, `portasm_JN516x.S`, `port_JN516x.c`).
3. **Config regeneration with v2395 tools** — `PDUMConfig` and `ZPSConfig`
   (the v2395 shell+python3 generators) run and regenerate `pdum_gen.*` and
   `zps_gen.*` from the coordinator GP-proxy `.zpscfg`, resolving library
   context sizes against `libZPSAPL_LEGACY_JN516x.a`. Two host-portability
   fixes were required (committed, see below).
4. **Library selection** — `LEGACY=1` correctly selects
   `libZPSAPL_LEGACY_JN516x.a` + `-DLEGACY_SUPPORT`; `R23_UPDATES=0`/`WWAH=0`
   correctly omit `-DR23_UPDATES`/`-DWWAH_SUPPORT`; `GP_SUPPORT=1` pulls in
   `app_green_power.c`, the GP-proxy `.zpscfg`, and `-DCLD_GREENPOWER`.

The build proceeds through application C compilation and **links** the final
image; the 1840→2395 API deltas that surface there are resolved (Blocker A/B).

## Host-portability fixes applied to the v2395 SDK generators (committed)

Both are in `Tools/{ZPSConfig,PDUMConfig}/Source/*`:

1. **CRLF → LF** on the two generator scripts. They shipped with CRLF, so the
   `#!/bin/sh\r` shebang failed on macOS ("command not found" /
   "No such file or directory").
2. **`ZPSConfig` objdump path** — for `BIG_ENDIAN` (the JN516x/BA2 default) the
   tool hardcoded Windows separators (`ToolsDir\\bin\\ba-elf-objdump`),
   producing an invalid path on POSIX and a silent
   *"Unable to locate '.zps_apl_ZdoDefaultServerContextSize' section"*. Made
   `os.name`-aware, mirroring the existing `arm-none-eabi` else-branch.

## Blocker A — application uses SDK symbols that changed 1840→2395  [RESOLVED]

Resolved via a thin app-level overlay `app/Source/ZigbeeNodeControlBridge/
zcl_overlay/zigate_compat.{h,c}` (no OpenLumi ZCL forward-ported). Address-mode
extensions are additive constants over the SDK enum (BROADCAST=0x04, *_NO_ACK
0x06/0x07/0x08, per zigpy-zigate); WindowCovering renames map to
`E_CLD_WC_CMD_*` with axis-split payload wrappers onto the v2395 Lift/Tilt send
functions; APDU diagnostics wrap stock `PDUM_u16APduGetCrtUse/MaxUse`. A genuine
latent v2395 SDK typo in `WindowCoveringCommands.c` (GoToTiltValueSend defined
with the Lift payload type) was fixed to match its header. Original delta table
retained below for provenance.

First failing translation unit: `app/Source/ZigbeeNodeControlBridge/app_Znc_cmds.c`.
Representative errors (v2395):

| Symbol used by app (1840)                              | v2395 status | Notes / candidate |
|--------------------------------------------------------|--------------|-------------------|
| `tsZLO_ControlBridgeDevice.sTimeServerCluster`         | removed      | Time server not in stock v2395 ControlBridge device (see Blocker B). |
| `E_CLD_WINDOWCOVERING_CMD_UP_OPEN` / `_DOWN_CLOSE` / `_STOP` | removed | v2395 `WindowCovering.h` differs; command enum renamed/removed. |
| `E_CLD_WINDOWCOVERING_CMD_GO_TO_LIFT_VALUE` / `_TILT_VALUE` | removed | Payloads are now `tsCLD_WindowCovering_GoToLiftValuePayload` / `...TiltValuePayload` (not `..._GoToValueRequestPayload`). |
| `E_CLD_WINDOWCOVERING_CMD_GO_TO_LIFT_PERCENTAGE` / `_TILT_PERCENTAGE` | removed | Payloads now `tsCLD_WindowCovering_GoToLiftPercentagePayload` / `...TiltPercentagePayload`. |
| `tsCLD_WindowCovering_GoToValueRequestPayload`         | removed      | Split into Lift/Tilt payload structs. |
| `tsCLD_WindowCovering_GoToPercentageRequestPayload`    | removed      | Split into Lift/Tilt payload structs. |
| `ZPS_E_ADDR_MODE_BROADCAST`                            | removed      | v2395 `zps_apl_af.h` keeps only `BOUND/GROUP/IEEE/SHORT`. ZCL-layer analogues exist: `E_ZCL_AM_BROADCAST`. |
| `ZPS_E_ADDR_MODE_BOUND_NO_ACK`                         | removed      | ZCL analogue `E_ZCL_AM_BOUND_NO_ACK`. |
| `ZPS_E_ADDR_MODE_SHORT_NO_ACK`                         | removed      | No direct v2395 analogue; needs semantic mapping. |
| `ZPS_E_ADDR_MODE_IEEE_NO_ACK`                          | removed      | ZCL analogue `E_ZCL_AM_IEEE_NO_ACK`. |

These are **not blind renames**: the address-mode and window-covering command
mappings change wire/dispatch semantics and must be validated (the coordinator
serialises these to the host protocol). Expect further deltas in the larger
units after `app_Znc_cmds.c` is resolved (`app_zcl_event_handler.c`,
`app_start.c`, `app_general_events_handler.c`, `app_green_power.c`).

## Blocker B — OpenLumi modified the SDK ZCL device/cluster layer  [RESOLVED]

Resolved by adapting the app to stock v2395 rather than forward-porting the
OpenLumi ZCL. The Time "server" (used by the app only as a host UTC counter,
not a network-registered cluster) is provided as standalone overlay storage
`sZigateTimeServerCluster`. Private OpenLumi quirks that depend on the modified
SDK ZCL — OnOff 0xFD "Lora tap", IKEA remote Scenes commands — are gated behind
`ZIGATE_ENABLE_OPENLUMI_PRIVATE_QUIRKS` (default OFF); the private Legrand Basic
attribute is disabled. The real stock cluster
`GENERAL_CLUSTER_ID_MULTISTATE_INPUT_BASIC` is resolved by including
`MultistateInputBasic.h`. Historical analysis retained below.

Stock v2395 `Components/ZCL/Devices/ZLO/Include/control_bridge.h` does **not**
contain the clusters the OpenLumi app expects. The OpenLumi 1840 device added,
among others: **Time server**, **WindowCovering client**, **Thermostat
server/UI-config client**, **IAS Zone server**, **Electrical Measurement**,
**Multistate in/out**, **Binary Input**, **PowerConfiguration client**,
**AnalogInput client**, **PressureMeasurement client**, and private
**Philips**/**Terncy** clusters.

Scope of the ZCL divergence between the OpenLumi-1840 tree and stock-2395:
**337 files differ substantively** (beyond CRLF) across `Components/ZCL`. This
mixes OpenLumi's additions with NXP's own 1840→2395 evolution, and cannot be
cleanly three-way separated without a pristine NXP-1840 ZCL tree.

### Recommended strategy (matches the "clean subtree + patch series" design)

Adapt the **application** to the **stock v2395** ZCL/device layer rather than
dragging the OpenLumi-1840 ZCL forward (which would also risk ABI mismatch with
the v2395 prebuilt `libZPSAPL`/`libZCL` binaries). Concretely:

1. Add the ZiGate-specific clusters the app needs as a **thin, self-contained
   overlay** compiled into the app (e.g. an `app/Source/ZigbeeNodeControlBridge/
   zcl_overlay/` providing Time server, WindowCovering client, private
   Xiaomi/Philips/Terncy clusters, IKEA scenes), instead of editing the SDK.
2. Re-express the app's ControlBridge device registration against stock v2395
   `control_bridge.{c,h}` plus the overlay clusters.
3. Resolve Blocker A symbol deltas with validated mappings.
4. Add protocol extensions and safety fixes as reviewable commits.

## Diagnostic ABI — proto 1.2 / build rev 4

- Protocol **1.2**, build **rev 4**; capability response `0x8D0F`.
- **Canonical TX-power semantics (rev4)** — corrected from rev3 per the
  physical HIL + MiniMac disassembly: in the linked MiniMac path **0x40 is NOT
  round-trippable** (SET sign-extends the low 6 bits to 0; GET returns a
  sign-extended i8, e.g. code −8 reads back as `0xFFFFFFF8`). Therefore:
  - **SET** (`app_ahi_commands.c`) accepts only exact non-clamping codes
    `0x00..0x0A` (positive 0..10) and `0x20..0x3F` (6-bit two's-complement
    −32..−1); it **rejects** `0x0B..0x1F` and `0x40`+. A rejected SET emits the
    stock status frame only, no value frame.
  - **`0x8806`/`0x8807`** (`app_Znc_cmds.c`) return `byte0 = GET & 0x3F` (the
    canonical six-bit code) and `byte1 = legacy mapped level` derived from that
    six-bit code, then the appended LQI byte. (rev3 wrongly echoed the full
    raw low byte incl. the phantom 0x40 bit.)
  - **General diag** (`0x0D1F`) TX fields are `[six-bit code][legacy level]
    [signed six-bit code]`, where signed = `(six & 0x20) ? six-64 : six`. All
    truthful; no phantom 0x40.
  - Wire shapes are byte-length-stable; only the byte *values*/validation
    changed, hence the proto-minor + build-rev bump (host validator must move
    to 1.2 / rev 4).
- **TCLK 0x0D00 diagnostic REMOVED** (security-sensitive). The feature exported
  the full internal TCLK/APS-security negotiation state over UART and
  interposed four ZPS crypto functions via `-Wl,--wrap`
  (`zps_eAplApsmeConfirmKeyReqRsp`, `zps_eAplSecEncryptPacket`,
  `zps_vGenerateHashForVerifiedKey`, `ZPS_u16NwkNibFindNwkAddr`). It exported no
  raw key/hash bytes and did no PDM restore, but instrumenting the crypto path
  and surfacing the internal security state machine is security-sensitive, so
  `tclk_diagnostic.{c,h}`, the four `--wrap` link flags, the 0x0D00 handler and
  all four instrumentation call sites were deleted. The `0x0D00` request ID is
  retained as reserved and now replies `0x8000` status "unhandled". The three
  general-diag TCLK bytes are retained in the 0x0D1F layout but are now always
  `NA` with `DIAG_GENDIAG_FLAG_TCLK_UNAVAILABLE` asserted.

## Validated safety fixes  [DONE]

- **No hidden Xiaomi mutation** — removed the join-time global Node Descriptor
  `u16ManufacturerCode` rewrite in `app_general_events_handler.c`
  (`NWK_NEW_NODE_HAS_JOINED`); `DEVICE_ANNOUNCE` emission preserved.
- **256-byte ZCL serialization OOB** — the individual read/report attribute
  loop in `app_zcl_event_handler.c` now stops before the next worst-case
  (uint64) element would exceed the 256-byte stack tx buffer with the appended
  LQI byte reserved. Truncation is safe; the previous behaviour could corrupt
  the stack for long strings / large arrays.
- **Zero-init IKEA payloads** — the IKEA remote-scene path is compiled out
  (stock v2395 lacks the modified-SDK custom command; no payload is allocated
  in this baseline). The zero-init requirement is documented in-code at the
  gate for anyone enabling the quirk.
- **`APP_MigratePDM` disabled** — `app_start.c` keeps `//APP_MigratePDM();`
  (call site commented out). No raw migration runs.

## Negotiated manufacturer-code command  [DONE]

Implemented `0x0D16`/`0x8D16` (capability bit `1 << 10`, set only because the
handler exists) in `custom_diag.{c,h}` with dispatch in `app_Znc_cmds.c` and
the enum in `SerialLink.h`. Ops GET / SET / RESTORE_DEFAULT act on the single
**global** Node Descriptor via public `ZPS_psGetLocalNodeDescriptor()` with
mandatory readback (SET returns OK only when the re-read code equals the
request). RESTORE targets the **shipped Node-Descriptor default 0x1147**
(`.zpscfg` `ManufacturerCode="4423"`), captured by snapshotting the live
descriptor **once before any SET** (single source of truth), with the
compile-time `DIAG_MANUF_CODE_SHIPPED_DEFAULT` (0x1147) only as a fallback. This
is deliberately **not** `ZCL_MANUFACTURER_CODE` (0x1037), which is the ZCL
attribute default and would restore the wrong value. No PDM write. The
firmware makes no per-device scoping claim — the host serialises coordinator
use via its lease (`zigbee/manufcode_lease.go`). Matches
`docs/DIAGNOSTIC_ABI_MANUFACTURER_CODE.md` and the Go host ABI. Stock /
handler-less builds leave the bit clear, so the host sees `ErrUnsupported`
(backward compatible).
