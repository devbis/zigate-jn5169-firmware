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
bin sha256 54eda516db00d7101ce3508bfb218e8e0755e9b14882fa3cff0c16228157cb28
elf sha256 da0180668b0fbd3ba051fe6a207bc1eb7f506f01ed13df315427f0575b76ed75
text=253604  data=2104  bss=30349          (ba-elf-size totals)
.text=249744  .rodata=3860  .data=2028  .bss=22348  .heap=2000  .stack=6000
```

Note on the `.elf` hash: the loadable image (`.bin`, and therefore every
allocatable section) has been unchanged at
`54eda516db00d7101ce3508bfb218e8e0755e9b14882fa3cff0c16228157cb28` since the
rev9 F1…F5 fixes. The `.elf` hash moved when the APDU-diagnostics prototypes
were wired up (`zigate_apdu_diag.h`) purely because adding `#include` lines
shifts line numbers in `.debug_line` and adds a file name to `.debug_str`.
Adding a prototype for a `uint8 (void)` function changes no codegen on this
ABI: `.text`, `.rodata`, `.data` and `.bss` are byte-for-byte identical, as
proven by the unchanged `.bin`.

Link-time RAM margin, computed with the linker's own expression
(`Chip/JN5168/Build/AppBuildEnd.ld:106`,
`ASSERT(LENGTH(ram) > ((_minimum_heap_end - ABSOLUTE(ORIGIN(ram))) + __stack_size))`):

```
LENGTH(ram)        = 0x07fb4    (32692)
ORIGIN(ram)        = 0x0400004c
_minimum_heap_end  = 0x04006754
__stack_size       = 0x01770    (6000)

used   = (0x04006754 - 0x0400004c) + 0x1770 = 0x6708 + 0x1770 = 0x7e78 (32376)
margin = 0x7fb4 - 0x7e78                    = 0x13c            = 316 bytes
```

Note on the historical figure: earlier revisions of this document quoted
**240 B**, obtained by subtracting `0x04000000` instead of `ORIGIN(ram)` from
`_minimum_heap_end` (i.e. `0x7fb4 - (0x6754 + 0x1770)`). That drops the
`0x4c` RAM origin offset and is therefore **76 B conservative**, not the
linker's number. The linker's margin has been **316 B** for every revision from
rev3 onwards, because `.data`/`.bss`/`_minimum_heap_end`/`__stack_size` have not
moved across rev3…rev9. The conservative 240 B figure is retained nowhere; all
RAM statements in this document now use the linker expression above.

(Hashes above are for **proto 1.2 / build rev 9**, which restores the
endpoint-1 raw-NCP transmit allowlist after physical HIL, on top of the rev8
correlated Green Power commissioning command `0x0D17`/`0x8D17`. Both BIN and
ELF were reproduced byte-for-byte by two clean local builds. rev9 is
descriptor-only on the ZPS side: eight `const uint16` cluster ids were added to
`s_au16Endpoint1OutputClusterList`, costing **16 B of `.text`** and **zero**
`.data`/`.bss`.
rev9 also carries the post-review fix that makes the endpoint-1 Time server
read-only over Zigbee (see "Time cluster ownership" below): one 58-byte
`.text` function (`bZigate_VetoRemoteTimeWrite`) plus its call site, **+60 B of
flash and zero `.data`/`.bss`**, so the link-time RAM margin is unchanged at
316 B. Neither the host wire ABI, `DIAG_PROTO_MINOR`, `DIAG_BUILD_REVISION`
nor `DIAG_FW_BUILD_ID` (`0x01014525`) changed, so the fix was folded into rev9
in place rather than widened to a rev10 — exactly as rev8 was corrected in
place. This is only legitimate because rev9 was never flashed or published:
its previous artifact `bin 457c49fc… / elf 417f09f2…` at `text=253544` exists
only in the working tree and appears nowhere in committed history.
The rev8 image was `bin 2bc2b354… / elf 17ba7210…` at `text=253528`; rev8 adds
a 32-bit per-request transaction id to that one command
(request 3 → 7 bytes, response 5 → 9 bytes) and costs 56 B of `.text` and
**zero** `.bss`/`.data`.
rev8 was never flashed or published, so its ABI was corrected in place before
release rather than being widened to a rev9: the byte-wide transaction id of
the first rev8 draft (request 4 / response 6 bytes) could wrap among callers
queued on the host's serialized request lock, and the same pass fixed a
one-byte stack overflow in the diagnostic `0x8000` status helper
(`au8Status[8]` → `[9]`, because `vSL_WriteMessage()` writes the link-quality
byte at `pu8Data[8]`).
The rev7 image, whose 0x8D17 response carried no correlation field, was
`bin c612e896… / elf b1457e86…` at `text=253464`.
The rev6 image, which advertised the GP capability bit without a command
behind it, was `bin 3f601b06… / elf d54e19b7…` at `text=253092`.
The rev4 image before the additive GP capability bit was
`bin e4f00796… / elf d903dc16…`.
The earlier rev3 image was `bin 7a878ca6… / elf 872b6d5d….`)

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

### Implicit function declarations are forbidden in the diagnostics paths

OpenLumi called the APDU-pool helpers `u8GetApduUse()` / `u8GetMaxApdu()`
without any prototype in scope. Under C89 rules GCC then invents
`int u8GetApduUse()`, which assumes the wrong return type and disables
argument checking. It happened to be harmless here — both functions really
return `uint8`, every call site immediately truncates the result to one octet
via `ZNC_BUF_U8_UPD`, and the `.bin` is unchanged now that the prototypes are
visible — but it is exactly the class of defect that turns into a silent
wrong-value or stack-corruption bug the moment a signature changes.

Fix, in three parts:

1. **One declaration, in a dependency-free header.**
   `zcl_overlay/zigate_apdu_diag.h` declares both symbols and includes
   `<jendefs.h>` and nothing else, so it cannot participate in an include
   cycle. `zigate_compat.h` now *includes* it instead of restating the
   prototypes, so there is exactly one declaration of each symbol in the tree
   and the definitions in `zigate_compat.c` are checked against it.
   The diagnostics translation units (`custom_diag.c`,
   `app_general_events_handler.c`) include the small header directly rather
   than `zigate_compat.h`, which would have dragged `zcl_options.h`, `zcl.h`,
   `ZclTime.h`, `WindowCovering.h`, `control_bridge.h` and
   `MultistateInputBasic.h` into the host-diagnostics path for the sake of two
   `uint8 (void)` prototypes.
2. **Hard build failure on recurrence.** `DIAG_STRICT_OBJS` in
   `app/Build/ZigbeeNodeControlBridge/Makefile` adds
   `-Werror=implicit-function-declaration` to `custom_diag.o`,
   `app_general_events_handler.o`, `app_zcl_event_handler.o`,
   `app_Znc_cmds.o` and `zigate_compat.o`. It is scoped per object (the same
   pattern as `WindowCovering.o: CFLAGS += -DCLD_WINDOW_COVERING`) because the
   stock SDK sources in this tree are *not* clean under this flag — e.g.
   `Components/ZCL/Clusters/OTA/Source/OTA_common.c:261` calls
   `OTA_SetNewImageFlag` implicitly. That is stock v2395 code, out of scope,
   and left untouched.
3. **Machine-checked.** `scripts/check.sh` asserts that the header declares
   both symbols, that it includes only `jendefs.h`, that each symbol is
   declared in exactly one file, and that *every* `.c` calling them both
   includes a declaring header and appears in `DIAG_STRICT_OBJS`. The caller
   set is discovered by grep, so a new call site in a new file fails the check
   until it is wired up.

After the fix the application sources emit **zero**
`-Wimplicit-function-declaration` diagnostics.

### RAM budget note (JN5169 32 KB SRAM)

The stock GP-proxy coordinator `.zpscfg` shipped `RoutingTableSize="255"`
(a 3060-byte route table) which overflowed SRAM by 1428 B at link
(`ASSERT ... "Possible overflow of RAM"`). It was right-sized to `70` to match
the four sibling coordinator `.zpscfg` variants in this tree, recovering
2208 B of BSS. Current margin against the link-time RAM assert is small
(**316 B**, derived above with the linker's own `ORIGIN(ram)`-relative
expression); new features that add BSS must be checked against that assert.

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
OpenLumi ZCL. A minimal endpoint-1 overlay now registers a real **Time
server**, **Window Covering client**, and **IAS Warning Device client**. The
Time factory uses the existing `sZigateTimeServerCluster`, so network reads,
host SET/GET, and the existing 1 Hz increment all share the same UTC storage.
The two client instances make the existing Window Covering and IAS WD serial
send paths pass v2395's local-cluster lookup instead of returning
`E_ZCL_ERR_CLUSTER_NOT_FOUND`.

The overlay appends three instances to the stock ControlBridge instance array;
the stock registration routine remains unchanged and includes them through
its existing `sizeof` count. Compile-time offset checks enforce that the three
instances stay contiguous at the array tail. Enabling the v2395 Window
Covering client exposed two stock implementation defects (a missing client
attribute-definition comma and client initialization cast to the server
type); both were corrected to match `WindowCovering.h`.

Private OpenLumi quirks that depend on the modified SDK ZCL — OnOff 0xFD
"Lora tap", IKEA remote Scenes commands — are gated behind
`ZIGATE_ENABLE_OPENLUMI_PRIVATE_QUIRKS` (default OFF); the private Legrand
Basic attribute is disabled. The real stock cluster
`GENERAL_CLUSTER_ID_MULTISTATE_INPUT_BASIC` is resolved by including
`MultistateInputBasic.h`. Historical analysis retained below.

### Endpoint-1 descriptor/runtime reconciliation

The selected GP Proxy coordinator `.zpscfg` advertises the runtime server set
**Basic, Groups, On/Off, OTA, Time**, and the endpoint-1 **OutputClusters**
list documented under "raw-NCP transmit allowlist" below. Groups and On/Off
server descriptors were made discoverable. The `0xFFFF` Default input remains
non-discoverable because ZiGate raw/hybrid APS forwarding uses it as the
generic endpoint-1 sink.

False *input* (server) descriptor entries were removed in rev6 rather than
spending the remaining RAM on unsupported instances, and that pruning stands:

- input-side client-only entries (Colour, Identify, Level, Scenes, Door Lock,
  Metering, IAS Zone, Thermostat, measurement clients, Diagnostics and ZLL
  Commissioning) were removed from the server list;
- unsupported input servers (Power Configuration, Thermostat UI, Appliance
  Statistics, Electrical Measurement, Illuminance Level, Occupancy and
  Pressure) were removed.

### Endpoint-1 raw-NCP transmit allowlist (rev9, corrected after HIL)

rev6 also pruned the **output** list on the same "no local client instance =
false advertisement" reasoning. Physical HIL after the host-side `0x0530`
fixes shows that reasoning does not hold for an NCP:

- a raw Basic `0x0000` read succeeds — and it succeeds *only* because Basic
  survived the pruning as an output cluster;
- a raw Power Configuration `0x0001` read or configure-reporting is rejected
  **locally by the JN5169** with APS status `0xA3`
  (`ZPS_APL_APS_E_ILLEGAL_REQUEST`) before anything is transmitted, because
  endpoint 1 no longer lists `0x0001` as an output cluster.

The ZPS APS layer therefore treats an endpoint's OutputClusters list as the
**allowlist for raw, host-originated transmissions**, independently of whether
a firmware-resident ZCL client instance exists. In this NCP the host is the
Zigbee controller, so those entries are a truthful statement of what the
host+NCP pair can originate, not a claim about firmware ZCL objects. rev9
restores the entries the hub legitimately originates:

| Cluster | Id | Why the hub originates it |
| --- | --- | --- |
| Power Configuration | `0x0001` | battery reads and default battery-percentage reporting configuration |
| Multistate Input | `0x0012` | converter/interview attribute reads |
| OTA Upgrade | `0x0019` | host-driven OTA server frames (Image Notify, block/query responses) are sent through raw `0x0530` |
| Thermostat UI Configuration | `0x0204` | display-mode / keypad-lockout reads and writes |
| Illuminance Level Sensing | `0x0401` | level-status / target-level reads |
| Pressure Measurement | `0x0403` | measured-value reads and reporting setup |
| Occupancy Sensing | `0x0406` | occupancy reads and reporting setup |
| Electrical Measurement | `0x0B04` | power/voltage/current reads and reporting setup |

Kept from before: Basic, Identify, Groups, Scenes, On/Off, Level Control,
Colour Control, Door Lock, Metering, IAS Zone, Thermostat, Illuminance
Measurement, Relative Humidity, Temperature, Diagnostics, Window Covering,
IAS Warning Device.

**Appliance Statistics `0x0B03` was deliberately NOT restored**: the host has
no decoder, converter, interview or command path for it — only a cluster-name
string — so no legitimate origination exists to authorize.

Two invariants this must not violate, both asserted by `scripts/check.sh`:

1. **No ZCL runtime instances are added for raw APS.** An output-descriptor
   entry authorizes the NCP to *originate* a cluster through `0x0530`; it does
   not require, and must not be paired with, a local cluster instance. Local
   instances remain necessary only where the *firmware itself* calls
   `eZCL_CustomCommandSend` (Window Covering client, IAS WD client) or answers
   network reads (Time server) — exactly the three overlay instances.
2. **Flash/const only.** The additions land in
   `s_au16Endpoint1OutputClusterList` (`const uint16`, .rodata): `text`
   253528 → 253544 (+16 B = 8 × 2 B), `data` 2104 and `bss` 30349 unchanged,
   link-time RAM margin still 316 B. (The final rev9 image is `text` 253604
   because of the +60 B Time-write veto described above; that is also
   `.text`-only and leaves `data`/`bss`/margin unchanged.)

Endpoint 242 Green Power descriptors and all ZDP endpoint-0 descriptors are
unchanged. The raw APS serial command itself is unchanged.

### Time cluster ownership — host-owned, read-only over Zigbee

`sZigateTimeServerCluster` (`zcl_overlay/zigate_compat.c`) is the single UTC
value shared by the host protocol and the endpoint-1 ZCL Time server
(cluster `0x000A`). Its access model is:

| Path | Direction | Allowed |
|---|---|---|
| `E_SL_MSG_SET_TIMESERVER` (`0x0016`, UART host) | write | **yes** |
| 1 Hz increment in `app_start.c` | write | **yes** |
| `E_SL_MSG_GET_TIMESERVER` (`0x0017`, UART host) | read | **yes** |
| Remote ZCL Read Attributes, EP1 / `0x000A` | read | **yes** |
| Remote ZCL Write Attributes, EP1 / `0x000A` | write | **no** — ZCL status `0x7e` NOT_AUTHORIZED |

Why the veto is required. `ZclTime.h` marks `Time` and `TimeStatus`
`(E_ZCL_AF_RD | E_ZCL_AF_WR)` when `TIME_SERVER` is defined, and although
`sCLD_Time` declares `E_ZCL_SECURITY_APPLINK`, `zcl.c:128` initialises
`psZCL_Common->eSecuritySupported` to `E_ZCL_SECURITY_NETWORK` and this
application never calls `eZCL_SetSupportedSecurity()`, so `zcl_event.c:653-654`
clamps the cluster's requirement back down to NETWORK. Registering the Time
server for network *reads* therefore also opened a network *write* path: any
node holding only the network key could rewrite the clock the host reads back
over `0x0017`. Before rev9 registered the server this storage had no network
path at all, so this was a newly introduced surface, not an inherited one.

How the veto works. `bZigate_VetoRemoteTimeWrite()` handles
`E_ZCL_CBET_CHECK_ATTRIBUTE_RANGE` and sets
`uMessage.sIndividualAttributeResponse.eAttributeStatus =
E_ZCL_ERR_ATTRIBUTES_ACCESS` for any Time **server** instance.
`zcl_WriteAttributesRequestHandle.c:290-294` maps that to
`E_ZCL_CMDS_NOT_AUTHORIZED` (`0x7e`), and the mutation at `:324-337` is gated
on `u8errorCode == E_ZCL_CMDS_SUCCESS`, so **the attribute is never written**.
For the *undivided* write variant the range check runs on pass 0 and clears
`bNoErrors` (declared once at `:133`, never reset), which gates the pass-1
write through `(bNoErrors || !bIsUndivided)` — the whole undivided record set
is refused.

Why it cannot be bypassed, and cannot misfire:

- `E_ZCL_CBET_CHECK_ATTRIBUTE_RANGE` has exactly **one** emitter in the SDK,
  the remote Write Attributes handler. Host `SET` and the 1 Hz increment write
  the struct directly and never traverse ZCL, so they are structurally
  unreachable from the veto.
- The call is the **first statement** of `APP_ZCL_cbEndpointCallback`, above the
  `RAW_MODE_ON` block that forwards the indication to the host and returns
  early. Placing it after that block would let raw mode skip authorization
  entirely.
- Both properties are machine-checked by `scripts/check.sh`: it asserts the
  single-emitter property, the `E_ZCL_ERR_ATTRIBUTES_ACCESS → NOT_AUTHORIZED`
  mapping, that the host SET/increment still write the struct directly, and —
  by line-number ordering inside the function body — that the veto call
  precedes the `RAW_MODE_ON` block with no `return` before it. The call must
  appear as a real `(void)…;` statement, so a comment mentioning the function
  does not satisfy the assertion.
- Three compile-time asserts in `zigate_compat.c` pin
  `E_ZCL_CMDS_SUCCESS == 0`, `E_ZCL_ERR_ATTRIBUTES_ACCESS != E_ZCL_CMDS_SUCCESS`
  and `E_ZCL_CMDS_NOT_AUTHORIZED == 0x7e`, so an SDK renumbering breaks the
  build instead of silently degrading the veto into a soft `INVALID_VALUE`.

Cost: 58 B of `.text` for the function plus its call site (+60 B of flash
total), zero `.data`/`.bss`, no change to the host wire ABI.

### OTA build-variable reality

The target still passes `OTA=0`, but the Makefile does not consume that
variable to disable OTA. `zcl_options.h` unconditionally defines
`CLD_OTA`/`OTA_SERVER`, OTA sources are linked, and endpoint 1 truthfully
advertises the OTA server. Thus `OTA=0` currently **does not disable OTA**.

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

## Diagnostic ABI — proto 1.2 / build rev 9

- Protocol **1.2**, build **rev 9**; capability response `0x8D0F`;
  deterministic build id `0x01014525` (rev8 was `0x01014524`, rev7
  `0x0101452B`; only the build revision changed).
- **rev9 is descriptor-only.** No wire structure, command encoding or
  capability bit changed; the revision exists so a host can tell from the
  build id whether the image can originate the restored endpoint-1 output
  clusters through raw `0x0530` at all (see "Endpoint-1 raw-NCP transmit
  allowlist" above). No ZCL runtime instance, RAM pool, table size or security
  surface was touched.
- **Green Power commissioning** is capability bit `1 << 3`. Since rev7 it is
  backed by a real negotiated command, `0x0D17`/`0x8D17`, and the bit and the
  handler share one compile switch (`DIAG_HAVE_GP_COMMISSIONING`, derived from
  `CLD_GREENPOWER`) so no build can advertise the capability without
  implementing it. The firmware drives its **own** Green Power proxy
  commissioning state machine on the locally mapped GP endpoint
  (`GREENPOWER_END_POINT_ID = 2`) towards APS endpoint 242, and the SDK's 20 ms
  GP scheduler closes the window when the requested 1..255 s timeout expires.
  This replaces the rev5/rev6 arrangement where the bit was advertised with no
  command behind it and the host emulated the operation with a raw `0x0530`
  send-data frame aimed at `0xFFFC` — an acknowledged unicast encoding that the
  physical rev6 HIL rejected with `ZPS_APL_APS_E_NO_ACK` (`0xA6`), and which
  would not have opened the coordinator's local window even if it had been
  transmitted. See [`DIAGNOSTIC_ABI_GREEN_POWER.md`](DIAGNOSTIC_ABI_GREEN_POWER.md).
  GP shared-key programming remains deliberately unimplemented and
  unadvertised because no corresponding serial command exists.
- **Per-request correlation (rev8)** — the rev7 `0x0D17`/`0x8D17` pair carried
  no correlation field, and the stock serial protocol correlates a data
  response only by its response type. A valid but late `0x8D17` answering a
  request the host had already timed out was therefore indistinguishable from
  the answer to the *next* request and could be consumed by it, reporting a
  stale commissioning window as the current one. rev8 re-encodes only this
  command: the request is `version, transaction_id[4, big-endian], action,
  timeout_seconds` (7 bytes) and the response is
  `version, transaction_id[4, big-endian], status, effective_mode,
  effective_timeout, gp_status` (9 bytes). The id is 32 bits wide, encoded
  big-endian like the `0x0D0F` nonce, so a host counter cannot wrap onto the
  id of a still-outstanding transaction while callers queue on the host's
  serialized request lock; the first rev8 draft used a single byte and could.
  The firmware treats the id as opaque and echoes it on every response emitted
  for a structurally valid request, success or Green Power failure. It also
  emits `status = OK` if and only if the underlying GP call returned
  `E_ZCL_SUCCESS`, an invariant the host now enforces on receive. The protocol
  version stays 1.2 and every other command encoding is byte-identical; the
  command is capability-gated and hosts re-negotiate `0x0D0F` (and read the
  changed build id) before using it.
- **Diagnostic `0x8000` status buffer (rev8 release gate)** — `vDiagSendStatus()`
  in `custom_diag.c` serialises 8 payload bytes and `vSL_WriteMessage()` then
  writes the link-quality byte at `pu8Data[8]`, but the local buffer was
  `uint8 au8Status[8]`, so every diagnostic status frame overflowed its own
  stack frame by exactly one byte. The buffer is now `[9]`; `scripts/check.sh`
  asserts it.
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
  - **General diag** (`0x0D1F`) TX fields are `[six-bit code][six-bit code]
    [signed six-bit code]`, where signed = `(six & 0x20) ? six-64 : six`.
    Rev 6 makes both unsigned protocol-1.2 fields canonical; no phantom 0x40.
  - **The two representations deliberately DO NOT agree from rev6 onwards.**
    `0x8D1F` byte1 is the canonical raw six-bit register code; `0x8806`/`0x8807`
    byte1 is the **legacy mapped level** from the threshold ladder in
    `app_Znc_cmds.c:429-434` (`<=31 → 0`, `<=39 → 32`, `<=51 → 20`, else `9`).
    rev4 had kept them in agreement; rev6 diverged them on purpose and this is
    **not** a defect to be "harmonised": changing `0x8806`/`0x8807` would break
    deployed hosts that parse the legacy mapping, and changing `0x8D1F` would
    reintroduce a lossy mapping into the diagnostic path. Hosts must key the
    `0x8D1F` TX interpretation off **build revision ≥ 6** via
    `DIAG_FW_BUILD_ID`. No claim anywhere in this repository asserts that the
    two frames agree.
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
