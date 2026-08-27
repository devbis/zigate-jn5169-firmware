# Migration status — OpenLumi ZiGate v3.23 app → JN-SW-4170 v2395 SDK

Target build cell (the requested configuration):

```
JN5169 / JN516x / COORDINATOR / BAUD=115200
GP_SUPPORT=1  LEGACY=1  R23_UPDATES=0  WWAH=0  OTA=0  TRACE=0  DEBUG=NONE  DISABLE_LTO=1
```

Build entry point: `scripts/build.sh {clean|all}` (see header for overrides).

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

The build now proceeds all the way into **application C compilation**, which is
where the true 1840→2395 API deltas surface.

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

## Blocker A — application uses SDK symbols that changed 1840→2395

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

## Blocker B — OpenLumi modified the SDK ZCL device/cluster layer

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

## Patch series application (deferred until the app compiles)

The four patches under `patches/` target the OpenLumi path layout
(`ModuleRadio/Firmware/src/ZiGate/Source/ZigbeeNodeControlBridge/...`). For this
repo they require path remapping to `app/Source/ZigbeeNodeControlBridge/...`,
e.g.:

```sh
git apply -p6 --directory=app/Source/ZigbeeNodeControlBridge \
    patches/0100-custom-diagnostic-protocol.patch     # verify -p depth first
```

Order: `0001` → `0080` → `0090` → `0100`. The `0100` patch also edits the app
`Makefile` (adds `tclk_diagnostic.c` / `custom_diag.c` and the four `--wrap`
link flags); that hunk must be reconciled with `app/Build/.../Makefile`.

## Diagnostic ABI notes carried forward (unchanged semantics)

- Protocol **1.1**; capability response `0x8D0F`; read-only TCLK wrappers via
  `-Wl,--wrap` (no library member modified).
- **Canonical TX-power semantics (rev3):** `0x8806`/`0x8807` return
  `byte0 = full PIB raw (0x00..0x40)`, `byte1 = legacy mapped value` from
  `raw & 0x3f`, then the appended LQI byte. Hosts must not re-mask byte0.

## Requested behaviours already satisfied in the app source

- **`APP_MigratePDM` disabled** — `app_start.c` already has `//APP_MigratePDM();`
  (call site commented out). No raw migration runs.

## Requested behaviours still to implement (post-compile hardening)

- **256-byte ZCL serialization OOB** — audit `SerialLink.c` (`au8LogBuffer[256]`,
  `bSL_ReadMessage`) and the `ZNC_BUF_*_UPD` writers for the tx buffer bound.
- **Zero-init IKEA payloads** — `app_zcl_event_handler.c` IKEA remote scene path
  (`psIkeaRemoteSceneCustomPayload`, ~line 1207) must zero-initialise the
  payload before population.
- **Xiaomi coordinator manufacturer mutation** — make the global manufacturer-code
  override conditional so it is skipped when the host advertises support via the
  `0x0D0F` capability negotiation.
