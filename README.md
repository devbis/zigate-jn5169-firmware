# ZiGate JN5169 coordinator firmware

Public, standalone firmware tree for a **ZiGate v1 (JN5169)** Zigbee
**coordinator**, built on the **NXP JN-SW-4170 v2395** SDK, carrying the
OpenLumi ZiGate v3.23 application and our custom read-only diagnostic protocol.

> Status: **compiling baseline; hardware qualification pending.** The OpenLumi
> application builds and links reproducibly against SDK v2395 with Green Power
> Proxy enabled. See [`docs/MIGRATION_STATUS.md`](docs/MIGRATION_STATUS.md).

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

rev 9 changes no wire encoding at all. It restores endpoint 1's **raw-NCP
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

## Target build cell

```
JN5169 / JN516x / COORDINATOR / BAUD=115200
GP_SUPPORT=1  LEGACY=1  R23_UPDATES=0  WWAH=0  OTA=0  TRACE=0
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

Reproducibility: the wrapper pins `LC_ALL=C`, `TZ=UTC`, and
`SOURCE_DATE_EPOCH`.

GitHub Actions validates the public custom ABI and security invariants, then
builds the complete firmware with the pinned Linux AMD64 toolchain from
[`openlumi/BA2-toolchain`](https://github.com/openlumi/BA2-toolchain). The
workflow verifies both the toolchain archive digest and the expected BIN hash
before publishing the BIN, ELF, and map outputs as workflow artifacts.
Only the flashable BIN is required to be byte-identical across build hosts;
the ELF may contain host-dependent metadata.

## Provenance & licensing

NXP SDK files retain their original license headers. OpenLumi application code
and local changes remain identifiable through the preserved Git history. See
`LICENSES/`.

## Safety

This tree targets a coordinator that speaks a host serial protocol. Firmware
changes here are **not** hardware-validated in this repository; no device was
flashed. Validate on hardware before deployment.
