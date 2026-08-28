#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

sh -n scripts/build.sh

HEADER=app/Source/ZigbeeNodeControlBridge/custom_diag.h
DIAG=app/Source/ZigbeeNodeControlBridge/custom_diag.c
ZNC_CMDS=app/Source/ZigbeeNodeControlBridge/app_Znc_cmds.c
GP_SOURCE=app/Source/ZigbeeNodeControlBridge/app_green_power.c
SERIAL=app/Source/ZigbeeNodeControlBridge/SerialLink.h
MAKEFILE=app/Build/ZigbeeNodeControlBridge/Makefile
START=app/Source/ZigbeeNodeControlBridge/app_start.c
OVERLAY=app/Source/ZigbeeNodeControlBridge/zcl_overlay/zigate_compat.c
OVERLAY_H=app/Source/ZigbeeNodeControlBridge/zcl_overlay/zigate_compat.h
CONTROL_BRIDGE=Components/ZCL/Devices/ZLO/Include/control_bridge.h
ZPSCFG=app/Source/ZigbeeNodeControlBridge/ZigbeeNodeControlBridgeCoordinator_GP_Proxy.zpscfg

grep -Eq '#define[[:space:]]+DIAG_PROTO_MAJOR[[:space:]]+\(1U\)' "$HEADER"
grep -Eq '#define[[:space:]]+DIAG_PROTO_MINOR[[:space:]]+\(2U\)' "$HEADER"
grep -Eq '#define[[:space:]]+DIAG_BUILD_REVISION[[:space:]]+\(9U\)' "$HEADER"
grep -Eq '#define[[:space:]]+DIAG_CAP_BIT_GP_COMMISSIONING[[:space:]]+\(\(\(uint64\)1U\)[[:space:]]*<<[[:space:]]*3\)' "$HEADER"
grep -Eq 'E_SL_MSG_MANUFACTURER_CODE_REQ[[:space:]]*=[[:space:]]*0x0D16' "$SERIAL"
grep -Eq 'E_SL_MSG_MANUFACTURER_CODE_RSP[[:space:]]*=[[:space:]]*0x8D16' "$SERIAL"
grep -Eq 'E_SL_MSG_GP_COMMISSION_REQ[[:space:]]*=[[:space:]]*0x0D17' "$SERIAL"
grep -Eq 'E_SL_MSG_GP_COMMISSION_RSP[[:space:]]*=[[:space:]]*0x8D17' "$SERIAL"

# The Green Power capability bit and its handler share one compile switch, so
# no build can advertise 1<<3 without implementing 0x0D17.
grep -Eq '^#define[[:space:]]+DIAG_HAVE_GP_COMMISSIONING[[:space:]]+\(1\)$' "$HEADER"
grep -A1 -E '^#ifdef[[:space:]]+DIAG_HAVE_GP_COMMISSIONING$' "$HEADER" \
    | grep -Eq '^#define[[:space:]]+DIAG_CAP_GP_BITMAP[[:space:]]+DIAG_CAP_BIT_GP_COMMISSIONING$'
grep -q 'CUSTOMDIAG_vHandleGPCommission' "$HEADER"
grep -q 'CUSTOMDIAG_vHandleGPCommission' "$DIAG"
grep -q 'CUSTOMDIAG_vHandleGPCommission' "$ZNC_CMDS"
grep -q 'u8App_GP_SetProxyCommissioningMode' "$GP_SOURCE"

# rev8 correlated Green Power encoding: the request carries a 32-bit host
# transaction id and every response emitted for a structurally valid request
# echoes it. A response without the echo cannot be correlated, so a late frame
# from a timed-out transaction would be consumed by the next one. The id is
# 32 bits wide so a host counter cannot wrap between queued requests.
grep -Eq '#define[[:space:]]+DIAG_GP_COMMISSION_REQ_LEN[[:space:]]+\(7U\)' "$HEADER"
grep -Eq '#define[[:space:]]+DIAG_GP_COMMISSION_RSP_LEN[[:space:]]+\(9U\)' "$HEADER"
grep -Eq 'u32TransactionId[[:space:]]*=[[:space:]]*ZNC_RTN_U32\(pu8Rx,[[:space:]]*1\);' "$DIAG"
grep -Eq 'u8Action[[:space:]]*=[[:space:]]*pu8Rx\[5\];' "$DIAG"
grep -Eq 'u8Timeout[[:space:]]*=[[:space:]]*pu8Rx\[6\];' "$DIAG"
grep -Eq 'ZNC_BUF_U32_UPD[[:space:]]*\([[:space:]]*&s_au8DiagTx\[[[:space:]]*u8Length[[:space:]]*\],[[:space:]]*u32TransactionId,' "$DIAG"

# The 0x8000 status frame serialises 8 payload bytes and vSL_WriteMessage()
# writes the link-quality byte at pu8Data[8], so the local buffer must be 9.
grep -Eq 'uint8 au8Status\[9\];' "$DIAG"

# The capability bitmap must never be derived straight from CLD_GREENPOWER
# again (rev5 did that while no handler existed at all).
if grep -B2 -E '^#define[[:space:]]+DIAG_CAP_GP_BITMAP' "$HEADER" | grep -q 'ifdef[[:space:]]*CLD_GREENPOWER'; then
    echo "GP capability bitmap must derive from DIAG_HAVE_GP_COMMISSIONING" >&2
    exit 1
fi

grep -q 'eCLD_TimeCreateTime' "$OVERLAY"
grep -q 'eCLD_WindowCoveringCreateWindowCovering' "$OVERLAY"
grep -q 'eCLD_IASWDCreateIASWD' "$OVERLAY"
grep -q 'sIASWarningDeviceClient' "$CONTROL_BRIDGE"
grep -q -- '-DZIGATE_CONTROL_BRIDGE_OVERLAY' "$MAKEFILE"
grep -Eq '^WindowCovering\.o: CFLAGS \+= -DCLD_WINDOW_COVERING$' "$MAKEFILE"

# All descriptor assertions below are scoped to the <Endpoints Id="1"> block so
# a same-named entry on endpoint 0 or 242 cannot satisfy them.
EP1_CLUSTERS=$(awk '
    /<Endpoints Id="1"/      { in_ep = 1 }
    in_ep                    { print }
    in_ep && /<\/Endpoints>/ { exit }
' "$ZPSCFG")

if [ -z "$EP1_CLUSTERS" ]; then
    echo "could not isolate the endpoint-1 block in $ZPSCFG" >&2
    exit 1
fi

printf '%s\n' "$EP1_CLUSTERS" | grep -Eq '<InputClusters Cluster="Time"[[:space:]].*Discoverable="true"'
printf '%s\n' "$EP1_CLUSTERS" | grep -Eq '<OutputClusters Cluster="Window_Covering"[[:space:]].*Discoverable="true"'
printf '%s\n' "$EP1_CLUSTERS" | grep -Eq '<OutputClusters Cluster="IAS_Warning_Device"[[:space:]].*Discoverable="true"'

# rev9 raw-NCP transmit allowlist. ZPS treats the endpoint-1 OutputClusters
# list as the allowlist for host-originated raw APS transmissions (0x0530):
# a missing entry is rejected locally with APS 0xA3 ILLEGAL_REQUEST, which is
# how rev6/rev8 HIL failed Power Configuration reads and configure-reporting
# while Basic reads succeeded. These entries are const/flash descriptor data
# and must NOT be paired with new ZCL runtime instances.
#
# The membership test is anchored through the closing quote of the Cluster
# attribute, so a longer cluster name that merely has a required name as its
# prefix (e.g. "Thermostat" vs "Thermostat_UI") cannot satisfy it.
for cluster in \
    Basic \
    Power_Configuration \
    MultiStateInput \
    OTA \
    Identify \
    Groups \
    Scenes \
    On_Off \
    Level_Control \
    Color_Control \
    Door_Lock \
    Metering \
    IASZone \
    Thermostat \
    Thermostat_UI \
    MEASUREMENT_AND_SENSING_CLUSTER_ID_ILLUMINANCE_MEASUREMENT \
    MEASUREMENT_AND_SENSING_CLUSTER_ID_ILLUMINANCE_LEVEL_SENSING \
    MEASUREMENT_AND_SENSING_CLUSTER_ID_PRESSURE_MEASUREMENT \
    MEASUREMENT_AND_SENSING_CLUSTER_ID_OCCUPANCY_SENSING \
    MEASUREMENT_AND_SENSING_CLUSTER_ID_RELATIVE_HUMIDITY_MEASUREMENT \
    MEASUREMENT_AND_SENSING_CLUSTER_ID_TEMPERATURE_MEASUREMENT \
    Electrical_Measurements \
    Diagnostics \
    Window_Covering \
    IAS_Warning_Device
do
    if ! printf '%s\n' "$EP1_CLUSTERS" \
            | grep -Eq "<OutputClusters Cluster=\"$cluster\"[[:space:]]"; then
        echo "endpoint-1 raw transmit allowlist is missing $cluster" >&2
        exit 1
    fi
done

# The raw allowlist must stay descriptor-only: exactly three ZCL client/server
# instances are created by the overlay (Time server, Window Covering client,
# IAS WD client) for firmware-side eZCL_CustomCommandSend paths.
if [ "$(grep -c 'eCLD_[A-Za-z]*Create[A-Za-z]*(' "$OVERLAY")" -ne 3 ]; then
    echo "unexpected number of ZCL instances in the overlay" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Time cluster is host-owned and READ-ONLY OVER ZIGBEE.
#
# Registering the endpoint-1 Time server exposes sZigateTimeServerCluster to
# the network. ZclTime.h marks Time/TimeStatus (E_ZCL_AF_RD|E_ZCL_AF_WR), and
# although ZCL_Time declares E_ZCL_SECURITY_APPLINK, zcl.c defaults
# eSecuritySupported to E_ZCL_SECURITY_NETWORK and the app never calls
# eZCL_SetSupportedSecurity(), so zcl_event.c clamps the requirement down to
# NETWORK: without a veto, any node holding only the network key could rewrite
# the clock the host reads back over E_SL_MSG_GET_TIMESERVER.
#
# 1. The veto exists, is scoped to the Time cluster server, and denies with the
#    status the SDK maps to ZCL 0x7e NOT_AUTHORIZED before mutation.
grep -q 'bZigate_VetoRemoteTimeWrite' "$OVERLAY_H"
grep -Eq '^PUBLIC bool_t bZigate_VetoRemoteTimeWrite\(tsZCL_CallBackEvent \*psEvent\)$' "$OVERLAY"
grep -Eq 'E_ZCL_CBET_CHECK_ATTRIBUTE_RANGE' "$OVERLAY"
grep -Eq 'GENERAL_CLUSTER_ID_TIME' "$OVERLAY"
grep -Eq 'eAttributeStatus[[:space:]]*=' "$OVERLAY"
grep -Eq 'E_ZCL_ERR_ATTRIBUTES_ACCESS' "$OVERLAY"

# 2. The SDK contract the veto relies on is still present verbatim: the range
#    callback has exactly one emitter in the whole SDK, namely the remote Write
#    Attributes handler (so a host SET or the 1 Hz increment can never be
#    intercepted), and E_ZCL_ERR_ATTRIBUTES_ACCESS still maps to NOT_AUTHORIZED.
ZCL_WRITE=Components/ZCIF/Source/zcl_WriteAttributesRequestHandle.c
EMITTERS=$(grep -rl \
    'eEventType[[:space:]]*=[[:space:]]*E_ZCL_CBET_CHECK_ATTRIBUTE_RANGE' \
    --include='*.c' Components | sort -u)
if [ "$EMITTERS" != "$ZCL_WRITE" ]; then
    echo "E_ZCL_CBET_CHECK_ATTRIBUTE_RANGE emitters changed: ${EMITTERS:-none}" >&2
    exit 1
fi
grep -A2 -E 'E_ZCL_ERR_ATTRIBUTES_ACCESS[[:space:]]*==' "$ZCL_WRITE" \
    | grep -q 'E_ZCL_CMDS_NOT_AUTHORIZED'

# 3. The veto is invoked from APP_ZCL_cbEndpointCallback ABOVE the RAW_MODE_ON
#    host-forwarding block, and nothing returns before it. Raw mode therefore
#    cannot bypass authorization. Proven by line-number ordering inside the
#    function body rather than by mere presence, because presence alone would
#    still pass if the call were moved below the early return. Only a real
#    statement counts: the pattern requires the "(void)...;" call form so a
#    comment mentioning the function cannot satisfy the assertion.
EVENT_HANDLER=app/Source/ZigbeeNodeControlBridge/app_zcl_event_handler.c
awk '
    /^PRIVATE void APP_ZCL_cbEndpointCallback/ { body = 1; next }
    body && /^}/                               { body = 0 }
    !body                                      { next }
    /^[[:space:]]*\(void\)bZigate_VetoRemoteTimeWrite[[:space:]]*\(.*\);[[:space:]]*$/ {
        if (!veto) veto = NR
    }
    /u8RawMode[[:space:]]*==[[:space:]]*RAW_MODE_ON/ { if (!raw)   raw   = NR }
    /^[[:space:]]*return[[:space:]]*;/          { if (!veto && !early) early = NR }
    END {
        if (!veto) {
            print "APP_ZCL_cbEndpointCallback does not call bZigate_VetoRemoteTimeWrite" > "/dev/stderr"
            exit 1
        }
        if (!raw) {
            print "could not locate the RAW_MODE_ON block in APP_ZCL_cbEndpointCallback" > "/dev/stderr"
            exit 1
        }
        if (veto > raw) {
            print "Time write veto runs after the RAW_MODE_ON early return" > "/dev/stderr"
            exit 1
        }
        if (early) {
            print "APP_ZCL_cbEndpointCallback returns before the Time write veto" > "/dev/stderr"
            exit 1
        }
    }
' "$EVENT_HANDLER"

# 4. Host SET/GET must keep writing/reading the shared struct directly, i.e.
#    never through the vetoed ZCL Write Attributes path.
grep -Eq 'sZigateTimeServerCluster\.utctTime[[:space:]]*=' "$ZNC_CMDS"
grep -Eq 'sZigateTimeServerCluster\.utctTime[[:space:]]*\+\+[[:space:]]*;' "$START"
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Implicit declarations of the APDU-pool diagnostics helpers are forbidden.
#
# OpenLumi called u8GetApduUse()/u8GetMaxApdu() with no prototype in scope, so
# GCC fell back to the C89 `int f()` rule. The prototypes now live in exactly
# one place, in a header that depends only on jendefs.h and therefore cannot
# take part in an include cycle. Assert: (1) the header declares both symbols,
# (2) it includes nothing but jendefs.h, (3) no other header or source
# re-declares them, and (4) every translation unit that calls them both
# includes a declaring header and is compiled with
# -Werror=implicit-function-declaration.
APDU_H=app/Source/ZigbeeNodeControlBridge/zcl_overlay/zigate_apdu_diag.h

grep -Eq '^PUBLIC[[:space:]]+uint8[[:space:]]+u8GetApduUse[[:space:]]*\(void\);$' "$APDU_H"
grep -Eq '^PUBLIC[[:space:]]+uint8[[:space:]]+u8GetMaxApdu[[:space:]]*\(void\);$' "$APDU_H"

# (2) dependency-free: jendefs.h is the only #include in the header.
apdu_includes=$(grep -c '^[[:space:]]*#[[:space:]]*include' "$APDU_H")
if [ "$apdu_includes" != "1" ] || ! grep -Eq '^#include <jendefs\.h>$' "$APDU_H"; then
    echo "zigate_apdu_diag.h must include jendefs.h and nothing else" >&2
    exit 1
fi

# (3) exactly one declaration of each symbol across the whole application tree.
for sym in u8GetApduUse u8GetMaxApdu; do
    decls=$(grep -rlE "^PUBLIC[[:space:]]+uint8[[:space:]]+$sym[[:space:]]*\(void\);$" \
                app/Source | sort -u | wc -l | tr -d ' ')
    if [ "$decls" != "1" ]; then
        echo "$sym must be declared exactly once (found $decls declaring files)" >&2
        exit 1
    fi
done

# (4) every caller declares them and is built with the strict flag.
grep -Eq '^\$\(DIAG_STRICT_OBJS\): CFLAGS \+= -Werror=implicit-function-declaration$' "$MAKEFILE"

apdu_callers=$(grep -rlE 'u8Get(ApduUse|MaxApdu)[[:space:]]*\(' \
                   app/Source --include='*.c' | sort -u)
for caller in $apdu_callers; do
    # zigate_compat.c defines the wrappers; it sees the prototypes via
    # zigate_compat.h, which includes zigate_apdu_diag.h.
    if ! grep -Eq '^#include "zigate_(apdu_diag|compat)\.h"$' "$caller"; then
        echo "$caller calls the APDU diagnostics helpers without declaring them" >&2
        exit 1
    fi
    obj=$(basename "$caller" .c).o
    if ! awk -v obj="$obj" '
            /^DIAG_STRICT_OBJS[[:space:]]*=/ { inlist = 1 }
            inlist && $1 == obj             { found = 1 }
            inlist && $0 !~ /\\[[:space:]]*$/ { inlist = 0 }
            END { exit(found ? 0 : 1) }
        ' "$MAKEFILE"; then
        echo "$obj is missing from DIAG_STRICT_OBJS" >&2
        exit 1
    fi
done
# ---------------------------------------------------------------------------

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
