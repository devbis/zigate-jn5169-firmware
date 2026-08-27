#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

sh -n scripts/build.sh

HEADER=app/Source/ZigbeeNodeControlBridge/custom_diag.h
SERIAL=app/Source/ZigbeeNodeControlBridge/SerialLink.h
MAKEFILE=app/Build/ZigbeeNodeControlBridge/Makefile
START=app/Source/ZigbeeNodeControlBridge/app_start.c

grep -Eq '#define[[:space:]]+DIAG_PROTO_MAJOR[[:space:]]+\(1U\)' "$HEADER"
grep -Eq '#define[[:space:]]+DIAG_PROTO_MINOR[[:space:]]+\(2U\)' "$HEADER"
grep -Eq '#define[[:space:]]+DIAG_BUILD_REVISION[[:space:]]+\(4U\)' "$HEADER"
grep -Eq 'E_SL_MSG_MANUFACTURER_CODE_REQ[[:space:]]*=[[:space:]]*0x0D16' "$SERIAL"
grep -Eq 'E_SL_MSG_MANUFACTURER_CODE_RSP[[:space:]]*=[[:space:]]*0x8D16' "$SERIAL"

if find app -type f \( -name 'tclk_diagnostic.c' -o -name 'tclk_diagnostic.h' \) | grep -q .; then
    echo "retired TCLK diagnostic source is present" >&2
    exit 1
fi

if grep -Eq -- '--wrap[=,[:space:]]' "$MAKEFILE"; then
    echo "security-sensitive linker interposition is enabled" >&2
    exit 1
fi

if grep -Eq '^[[:space:]]*APP_MigratePDM[[:space:]]*\(' "$START"; then
    echo "unsafe legacy PDM migration is enabled" >&2
    exit 1
fi

echo "source and ABI checks passed"
