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
bin sha256 7a878ca67e374596faed8392f12e0ad514d3d43377dd0ee9286f6121c4bcdafd
elf sha256 872b6d5d7e522367a4a6b4e38ef0d2dd4079dccc0715f4bf9b8860d7acb074c2
```

Determinism is pinned by `scripts/build.sh` (`LC_ALL=C`, `TZ=UTC`,
`SOURCE_DATE_EPOCH`). Both Blocker A and Blocker B are resolved; the validated
safety fixes, the custom diagnostic ABI and the negotiated manufacturer-code
command are implemented (see below).

### RAM budget note (JN5169 32 KB SRAM)

The stock GP-proxy coordinator `.zpscfg` shipped `RoutingTableSize="255"`
(a 3060-byte route table) which overflowed SRAM by 1428 B at link
(`ASSERT ... "Possible overflow of RAM"`). It was right-sized to `70` to match
the four sibling coordinator `.zpscfg` variants in this tree, recovering
2208 B of BSS. Current headroom is small (~0.5 KB); new features that add BSS
must be checked against the link-time RAM assert.

## What works today (baseline established)

1. **Repository structure** — v2395 SDK at the repo root with upstream history
   preserved; application isolated under `app/`; validated patch series under
   `patches/`.
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
4. Only then apply `patches/` (after path-remapping — see below).

## Patch series application  [APPLIED]

`patches/0100-custom-diagnostic-protocol.patch` is the comprehensive, final
snapshot (TCLK 0x0D00, capability negotiation, local neighbour/route/group
tables, general diag, canonical TX-power rev3, `SerialLink.h`,
`app_ahi_commands.c`, `app_zcl_event_handler.c`). It was applied with
`patch -p5` from `app/` (path prefix `ModuleRadio/Firmware/src/ZiGate/` →
`app/`). It adds `tclk_diagnostic.c` / `custom_diag.c` to the app `Makefile`
plus the four read-only `--wrap` link flags, reconciled with the overlay's
existing `APPSRC`/`INCFLAGS`/`vpath` additions.

`patches/0080-*` and `patches/0090-*` are **superseded earlier iterations**
(each recreates `tclk_diagnostic.{c,h}` from scratch); they are retained under
`patches/` for provenance only and are **not** applied. `0001-build-
reproducibility.patch` is already reflected in `scripts/build.sh`.

## Diagnostic ABI notes carried forward (unchanged semantics)

- Protocol **1.1**; capability response `0x8D0F`; read-only TCLK wrappers via
  `-Wl,--wrap` (no library member modified).
- **Canonical TX-power semantics (rev3):** `0x8806`/`0x8807` return
  `byte0 = full PIB raw (0x00..0x40)`, `byte1 = legacy mapped value` from
  `raw & 0x3f`, then the appended LQI byte. Hosts must not re-mask byte0.

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
request); RESTORE targets `ZCL_MANUFACTURER_CODE` (0x1037). No PDM write. The
firmware makes no per-device scoping claim — the host serialises coordinator
use via its lease (`zigbee/manufcode_lease.go`). Matches
`docs/DIAGNOSTIC_ABI_MANUFACTURER_CODE.md` and the Go host ABI. Stock /
handler-less builds leave the bit clear, so the host sees `ErrUnsupported`
(backward compatible).
