#!/bin/sh
###############################################################################
# Reproducible build wrapper for the ZiGate JN5169 coordinator firmware.
#
# Repository layout (see docs/PROVENANCE.md):
#   <repo>/                 JN-SW-4170 v2395 SDK (upstream history preserved)
#   <repo>/app/             Application subtree (OpenLumi ZiGate ControlBridge)
#   <repo>/patches/         OpenLumi + custom-diagnostic patch series
#
# Default build cell (overridable via environment):
#   JN5169 / JN516x / COORDINATOR / BAUD=115200
#   GP_SUPPORT=1  LEGACY=1  R23_UPDATES=0  WWAH=0  OTA=0  TRACE=0
#
# The BA2 toolchain (ba-elf-gcc 4.7.4 / binutils 2.22) is an *external*
# artifact and is NOT vendored in this repository. Point TOOLCHAIN_ROOT at the
# directory that contains "ba-elf-ba2/bin/ba-elf-gcc".
###############################################################################
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ACTION=${1:-all}

# --- External toolchain (override with TOOLCHAIN_ROOT=/path) ------------------
TOOLCHAIN_ROOT=${TOOLCHAIN_ROOT:-/Users/afaronov/.copilot/session-state/fa547e9c-bb89-4a9a-91c5-40847d5d13c7/files/zigate-tclk-diagnostic-20260812/toolchain}
TOOLCHAIN_PATH=${TOOLCHAIN_PATH:-ba-elf-ba2}
if [ ! -x "$TOOLCHAIN_ROOT/$TOOLCHAIN_PATH/bin/ba-elf-gcc" ]; then
    echo "ERROR: BA2 toolchain not found at $TOOLCHAIN_ROOT/$TOOLCHAIN_PATH/bin/ba-elf-gcc" >&2
    echo "       Set TOOLCHAIN_ROOT to the directory containing $TOOLCHAIN_PATH/." >&2
    exit 2
fi

# --- Python (xmltodict) for ZPSConfig/PDUMConfig generators -------------------
# The SDK generators are self-contained shell+python3 scripts; ensure the
# python3 they resolve provides xmltodict==0.13.0.
if [ -x "$ROOT/.venv/bin/python3" ]; then
    PATH="$ROOT/.venv/bin:$PATH"
    export PATH
fi
python3 -c "import xmltodict" 2>/dev/null || {
    echo "ERROR: python3 with xmltodict not available. Create .venv:" >&2
    echo "       python3 -m venv .venv && .venv/bin/pip install xmltodict==0.13.0" >&2
    exit 2
}

# --- Reproducibility ----------------------------------------------------------
export LC_ALL=C
export TZ=UTC
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1666030260}

# --- Build cell (all overridable) --------------------------------------------
JENNIC_CHIP=${JENNIC_CHIP:-JN5169}
JENNIC_CHIP_FAMILY=${JENNIC_CHIP_FAMILY:-JN516x}
NODE=${NODE:-COORDINATOR}
BAUD=${BAUD:-115200}
GP_SUPPORT=${GP_SUPPORT:-1}
LEGACY=${LEGACY:-1}
R23_UPDATES=${R23_UPDATES:-0}
WWAH=${WWAH:-0}
OTA=${OTA:-0}
TRACE=${TRACE:-0}
DEBUG=${DEBUG:-NONE}
DISABLE_LTO=${DISABLE_LTO:-1}

BUILD_DIR="$ROOT/app/Build/ZigbeeNodeControlBridge"
cd "$BUILD_DIR"

set -x
exec make -j1 "$ACTION" \
    SDK_BASE_DIR="$ROOT" \
    TOOL_COMMON_BASE_DIR="$TOOLCHAIN_ROOT" \
    TOOLCHAIN_PATH="$TOOLCHAIN_PATH" \
    JENNIC_CHIP="$JENNIC_CHIP" \
    JENNIC_CHIP_FAMILY="$JENNIC_CHIP_FAMILY" \
    NODE="$NODE" \
    BAUD="$BAUD" \
    GP_SUPPORT="$GP_SUPPORT" \
    LEGACY="$LEGACY" \
    R23_UPDATES="$R23_UPDATES" \
    WWAH="$WWAH" \
    OTA="$OTA" \
    TRACE="$TRACE" \
    DEBUG="$DEBUG" \
    DISABLE_LTO="$DISABLE_LTO"
