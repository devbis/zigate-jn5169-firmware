# Licensing and provenance notices

This repository combines three separately-licensed sources. Each retains the
license of its origin; nothing here relicenses upstream code.

## 1. NXP JN-SW-4170 SDK — repository root

Files under `Chip/`, `Components/`, `Platform/`, `Stack/`, `Tools/` (and
`build.txt`) originate from the NXP JN-SW-4170 SDK, obtained via
`https://github.com/igorlistopad/JN-SW-4170` at branch `v2395`
(commit `c49f2b2ea5239bb01b9283da81b62855509edce1`).

These files carry NXP's own license headers, which restrict use to NXP
products (JN516x / JN517x microcontrollers). The headers are preserved verbatim
in every file. Example header text (from the SDK Makefiles/sources):

> This software is owned by NXP B.V. and/or its supplier and is protected under
> applicable copyright laws. All rights are reserved. We grant You, and any
> third parties, a license to use this software solely and exclusively on NXP
> products [NXP Microcontrollers such as JN516x, JN517x]. ...
> Copyright NXP B.V. All rights reserved.

Do not redistribute the NXP SDK components outside the terms of that NXP
license.

### Local modifications to SDK files

Two host-portability fixes were made to the SDK config generators so they run
on macOS/POSIX (see `docs/MIGRATION_STATUS.md`):

- `Tools/ZPSConfig/Source/ZPSConfig` — CRLF→LF; `os.name`-aware `objdump` path.
- `Tools/PDUMConfig/Source/PDUMConfig` — CRLF→LF.

The `app/Build/ZigbeeNodeControlBridge/Makefile` (an OpenLumi/NXP application
Makefile) has a single structural change: `SDK_BASE_DIR` now defaults to the
repository root (the SDK's location in this layout).

## 2. OpenLumi ZiGate application — `app/`

Files under `app/` originate from `https://github.com/openlumi/ZiGate`
(a fork of `https://github.com/fairecasoimeme/ZiGate`) at tag `v3.23`
(commit `55f8592b0724f787e151cff49d2c64dfc854617f`). They carry NXP application
license headers (the ControlBridge application note) together with OpenLumi's
modifications. Upstream ZiGate is distributed by Fairecasoimeme / OpenLumi;
retain their notices.

## 3. Custom diagnostic protocol + hardening — `patches/`

The patch series under `patches/` is our own work (read-only TCLK diagnostics,
the negotiated diagnostic ABI, build reproducibility). It is applied on top of
the OpenLumi application and is subject to the same downstream constraints as
the code it modifies (NXP application license for the touched files).

---

No GitHub repository has been created or pushed for this tree. All history is
local.
