# ZiGate JN5169 coordinator firmware

Private, standalone firmware tree for a **ZiGate v1 (JN5169)** Zigbee
**coordinator**, built on the **NXP JN-SW-4170 v2395** SDK, carrying the
OpenLumi ZiGate v3.23 application and our custom read-only diagnostic protocol.

> Status: **migration in progress.** The repository, build system, and v2395
> config regeneration are in place and working; the OpenLumi application subtree
> is being ported from SDK build 1840 to v2395. See
> [`docs/MIGRATION_STATUS.md`](docs/MIGRATION_STATUS.md) for the exact blockers.

## Layout

```
<repo root>/          NXP JN-SW-4170 v2395 SDK (upstream git history preserved)
  Chip/ Components/ Platform/ Stack/ Tools/ build.txt
  app/                Application subtree (OpenLumi ZiGate ControlBridge)
    Source/{ZigbeeNodeControlBridge,Common}/
    Build/ZigbeeNodeControlBridge/Makefile
  patches/            Custom-diagnostic + reproducibility patch series (inputs)
  scripts/build.sh    Reproducible build wrapper
  docs/               PROVENANCE.md, MIGRATION_STATUS.md
  LICENSES/           Licensing / provenance notices
```

See [`docs/PROVENANCE.md`](docs/PROVENANCE.md) for exact upstream commits,
hashes, and licensing boundaries.

## Target build cell

```
JN5169 / JN516x / COORDINATOR / BAUD=115200
GP_SUPPORT=1  LEGACY=1  R23_UPDATES=0  WWAH=0  OTA=0  TRACE=0
```

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

## Provenance & licensing

Three separately-licensed inputs are kept distinct — NXP SDK (root), OpenLumi
application (`app/`), and our patches (`patches/`). NXP SDK files retain their
original NXP license headers. See `LICENSES/`.

## Safety

This tree targets a coordinator that speaks a host serial protocol. Firmware
changes here are **not** hardware-validated in this repository; no device was
flashed. Validate on hardware before deployment.
