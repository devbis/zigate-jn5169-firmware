# Provenance

This repository is a **private, standalone** ZiGate JN5169 coordinator firmware
tree. It is assembled from three independently-licensed sources, kept distinct
so provenance and licensing remain auditable.

## 1. SDK baseline — repository root (upstream history preserved)

- Upstream: `https://github.com/igorlistopad/JN-SW-4170`
- Branch: `v2395`
- Commit: `c49f2b2ea5239bb01b9283da81b62855509edce1` ("Fix makefiles for JN516x")
- `build.txt`: `Build Number 2395`
- Clone method: `git clone --single-branch --branch v2395`, then the clone
  contents (including `.git`) were promoted to the repository root so that the
  **full upstream commit history of the v2395 branch is preserved** as this
  repository's history. The application, docs, and scripts are added
  on top as new commits.

This is the NXP JN-SW-4170 SDK (JN516x / JN5169). It ships the ZPS stack,
ZCL component tree, BDB, PDM/PDUM, MAC/MMAC, and the prebuilt libraries used at
link time (`libZPSAPL_LEGACY_JN516x.a`, `libZPSGP_JN516x.a`, etc.).

## 2. Application subtree — `app/`

- Upstream: `https://github.com/openlumi/ZiGate`
  (fork of `https://github.com/fairecasoimeme/ZiGate`)
- Tag: `v3.23`
- Commit: `55f8592b0724f787e151cff49d2c64dfc854617f`
- Commit time: `2023-01-15T00:00:58+05:30`
- Exported pristine via `git archive` (no checkout of the source clone); the
  extracted tree matched a fresh extraction recursively. Input tar SHA-256:
  `ea756b7cc1d78c879e0a6de4996c2c5a8ee4ff37831b81e03898b68bf1dd70ec`.

`app/` contains only the OpenLumi ControlBridge **application** files, taken
from `ModuleRadio/Firmware/src/ZiGate/`:

| Repo path                                   | Source path (in ZiGate v3.23)                       |
|---------------------------------------------|-----------------------------------------------------|
| `app/Source/ZigbeeNodeControlBridge/`       | `.../src/ZiGate/Source/ZigbeeNodeControlBridge/`     |
| `app/Source/Common/`                        | `.../src/ZiGate/Source/Common/`                      |
| `app/Build/ZigbeeNodeControlBridge/Makefile`| `.../src/ZiGate/Build/ZigbeeNodeControlBridge/Makefile` |

The OpenLumi tree was originally built against its **own bundled JN-SW-4170
build 1840** SDK, which OpenLumi had modified at the ZCL device/cluster layer.
Those SDK-level modifications are **not** carried into this repository; the
migration target is stock v2395 (see `docs/MIGRATION_STATUS.md`).

## 3. Local protocol and hardening changes

The negotiated diagnostic/control ABI, reproducible build support, Green Power
port, and safety fixes are committed directly on top of the two upstream
histories. Superseded patch snapshots were intentionally removed because they
contained the retired TCLK instrumentation and obsolete protocol semantics.
Use `git log upstream/v2395..HEAD` to audit every local change.

The uncommitted rev17 work is local engineering on that pinned source: it
preserves the uncommitted rev11 TX-power/PDM changes, applies bounded parser
and SerialLink fixes based on rev10 physical reset observations, and adds the
read-only reset snapshot documented in `docs/RESET_DIAGNOSTIC_ABI.md`. Rev13
closed the boot-time TX restore race exposed by rev12 HIL; rev14 adds
RAM-only software-reset reason retention after rev13 HIL exposed an unrelated
reset storm. Rev15 adds retained fault registers after rev14 HIL identified
the reset path as a bus error; rev16 independently negotiates that context.
Rev17 guards the undefined APS key-index output identified from the rev16
fault address without modifying either prebuilt SDK archive. No Go host
source, parent-repository file, SDK binary, credential path, or hardware flash
operation is part of this source revision.

## Toolchain (external, not vendored)

- BA2 GCC `4.7.4`, GNU binutils `2.22` (`ba-elf-*`).
- CI uses OpenLumi release `ba2-toolchain-a4de652`, Linux AMD64 archive SHA-256
  `acfd927ccc6ecddf12a3fdf6b5fa28645a09bb7d2e48606f6d37628c15d15334`.
- Expected at `$TOOLCHAIN_ROOT/ba-elf-ba2/bin/`; the local default is
  `$HOME/toolchains/ba-elf-ba2/bin/`.
- The SDK config generators (`Tools/{ZPSConfig,PDUMConfig}/Source/*`) are
  self-contained `sh`+`python3` scripts and require `python3` with
  `xmltodict==0.13.0` and `lxml`. A repo-local `.venv/` (gitignored) is used.

## Isolation

- Target working directory: `/Users/afaronov/go_zboss-ncp/zigate-jn5169-firmware`.
- No hardware was connected, halted, reset, flashed, or erased. No PDM
  operation was performed.
- The tree is a standalone repository and is not added to any enclosing index.
