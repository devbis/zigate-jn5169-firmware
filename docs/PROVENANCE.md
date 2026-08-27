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
  repository's history. The application, patches, docs, and scripts are added
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

## 3. Custom diagnostic protocol + hardening — `patches/`

Validated patch series produced and reproducibly built during the prior
`zigate-tclk-diagnostic` work (GP_SUPPORT=0 baseline on the 1840 SDK):

| Patch                                      | Purpose                                             |
|--------------------------------------------|-----------------------------------------------------|
| `0001-build-reproducibility.patch`         | Deterministic build (drop timestamped `size.txt`).  |
| `0080-tclk-diagnostic.patch`               | Read-only TCLK diagnostic (link `--wrap`).          |
| `0090-tclk-diagnostic-uart.patch`          | UART diagnostic transport.                           |
| `0100-custom-diagnostic-protocol.patch`    | Negotiated diagnostic ABI (proto 1.1) + rev3 canonical TX-power semantics. |

These patches were authored against the OpenLumi path layout
(`ModuleRadio/Firmware/src/ZiGate/...`). They must be **path-remapped** to the
`app/...` layout before application (see `docs/MIGRATION_STATUS.md`). They are
preserved verbatim here as inputs; they are **not yet applied** because the
application subtree does not yet compile against v2395.

## Toolchain (external, not vendored)

- BA2 GCC `4.7.4`, GNU binutils `2.22` (`ba-elf-*`).
- Expected at `$TOOLCHAIN_ROOT/ba-elf-ba2/bin/` (default points at the prior
  session toolchain; override `TOOLCHAIN_ROOT`).
- The SDK config generators (`Tools/{ZPSConfig,PDUMConfig}/Source/*`) are
  self-contained `sh`+`python3` scripts and require `python3` with
  `xmltodict==0.13.0` and `lxml`. A repo-local `.venv/` (gitignored) is used.

## Isolation

- Target working directory: `/Users/afaronov/go_zboss-ncp/zigate-jn5169-firmware`.
- No hardware was connected, halted, reset, flashed, or erased. No PDM
  operation was performed.
- The enclosing `go_zboss-ncp` Git repository was **not** modified; this tree is
  a nested standalone repository and is intentionally not added to the parent
  index.
- No GitHub repository was created or pushed.
