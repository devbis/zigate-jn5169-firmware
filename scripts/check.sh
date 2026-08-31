#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

sh -n scripts/build.sh

HEADER=app/Source/ZigbeeNodeControlBridge/custom_diag.h
DIAG=app/Source/ZigbeeNodeControlBridge/custom_diag.c
APS_KEY_GUARD=app/Source/ZigbeeNodeControlBridge/zps_key_index_guard.c
ZNC_CMDS=app/Source/ZigbeeNodeControlBridge/app_Znc_cmds.c
GP_SOURCE=app/Source/ZigbeeNodeControlBridge/app_green_power.c
SERIAL=app/Source/ZigbeeNodeControlBridge/SerialLink.h
SERIAL_C=app/Source/ZigbeeNodeControlBridge/SerialLink.c
MAKEFILE=app/Build/ZigbeeNodeControlBridge/Makefile
START=app/Source/ZigbeeNodeControlBridge/app_start.c
GENERAL_EVENTS=app/Source/ZigbeeNodeControlBridge/app_general_events_handler.c
ZCL_EVENTS=app/Source/ZigbeeNodeControlBridge/app_zcl_event_handler.c
OVERLAY=app/Source/ZigbeeNodeControlBridge/zcl_overlay/zigate_compat.c
OVERLAY_H=app/Source/ZigbeeNodeControlBridge/zcl_overlay/zigate_compat.h
CONTROL_BRIDGE=Components/ZCL/Devices/ZLO/Include/control_bridge.h
ZPSCFG=app/Source/ZigbeeNodeControlBridge/ZigbeeNodeControlBridgeCoordinator_GP_Proxy.zpscfg
ZPS_GEN=app/Source/ZigbeeNodeControlBridge/zps_gen.c
OCB_EXP_H=app/Source/ZigbeeNodeControlBridge/ocb_experimental.h
OCB_EXP_C=app/Source/ZigbeeNodeControlBridge/ocb_experimental.c
AHI=app/Source/ZigbeeNodeControlBridge/app_ahi_commands.c
AHI_H=app/Source/ZigbeeNodeControlBridge/app_ahi_commands.h
PDM_IDS=app/Source/ZigbeeNodeControlBridge/PDM_IDs.h
BDB_STATE=Components/BDB/Source/Common/bdb_state_machine.c
README=README.md
MIGRATION=docs/MIGRATION_STATUS.md
OCB_DOC=docs/OCB_UART_ABI.md
RESET_DOC=docs/RESET_DIAGNOSTIC_ABI.md
ELF=app/Build/ZigbeeNodeControlBridge/ZigbeeNodeControlBridge_JN5169_GP_Proxy_COORDINATOR_115200.elf
BA2_BIN=${TOOLCHAIN_ROOT:-$HOME/toolchains}/${TOOLCHAIN_PATH:-ba-elf-ba2}/bin

# Coordinator TX power is an application-owned, versioned PDM setting. Keep
# the native MiniMac validation set exact. Restoration is anchored to the
# application main loop after BDB work, rather than inside a stack/BDB
# callback or at AF init. Restored-network boot also schedules the first
# application before serial RX can observe the default PIB value.
grep -Eq '^#define[[:space:]]+PDM_ID_APP_TX_POWER_V1_RESERVED[[:space:]]+0x11$' "$PDM_IDS"
grep -Eq '^#define[[:space:]]+PDM_ID_APP_TX_POWER[[:space:]]+0x12$' "$PDM_IDS"
if [ "$(grep -Ec '^#define[[:space:]]+PDM_ID_APP_[A-Z0-9_]+[[:space:]]+0x12$' "$PDM_IDS")" -ne 1 ]; then
    echo "PDM application record 0x12 is not unique" >&2
    exit 1
fi
if grep -q 'PDM_ID_APP_TX_POWER_V1_RESERVED' "$AHI"; then
    echo "legacy TX-power record 0x11 must remain abandoned" >&2
    exit 1
fi
grep -Eq '^#define[[:space:]]+APP_TX_POWER_RECORD_VERSION[[:space:]]+\(2U\)$' "$AHI"
grep -q '} PACK tsAPP_TxPowerRecord;' "$AHI"
grep -q 'sizeof(tsAPP_TxPowerRecord) == 5U' "$AHI"
grep -q 'psRecord->u8Check == u8APP_AHITxPowerRecordCheck(psRecord)' "$AHI"
grep -q 'u16BytesRead == sizeof(\*psRecord)' "$AHI"
grep -Eq 'u8TxPower <= 0x0A \|\|' "$AHI"
grep -Eq 'u8TxPower >= 0x20 && u8TxPower <= 0x3F' "$AHI"
grep -q 'PDM_eSaveRecordData(PDM_ID_APP_TX_POWER' "$AHI"
grep -q 'APP_vAHINotifyNetworkStarted' "$AHI_H"
grep -q 'APP_vAHIServicePersistedTxPower' "$AHI_H"
if grep -q 'TxPowerRestoreAttempted\|RestoreTxPowerOnce' "$AHI" "$AHI_H"; then
    echo "TX-power application must not be suppressed after the first network start" >&2
    exit 1
fi
grep -q 'if (!bAPP_AHIStoredTxPowerLoaded)' "$AHI"

if grep -q 'APP_vAHIServicePersistedTxPower' "$GENERAL_EVENTS"; then
    echo "TX-power restore must not run inside the forwarded stack-event callback" >&2
    exit 1
fi
grep -A12 'case BDB_EVENT_ZPSAF:' "$ZCL_EVENTS" \
    | grep -q 'ZPS_EVENT_NWK_STARTED'
grep -A8 'ZPS_EVENT_NWK_STARTED' "$ZCL_EVENTS" \
    | grep -q 'APP_vAHINotifyNetworkStarted();'
BDB_START=$(grep -n 'BDB_vStart();' "$START" | cut -d: -f1)
BOOT_TX_NOTIFY=$(grep -n 'APP_vAHINotifyNetworkStarted();' "$START" | cut -d: -f1)
MAIN_LOOP=$(grep -n 'APP_vMainLoop();' "$START" | cut -d: -f1)
if [ -z "$BDB_START" ] || [ -z "$BOOT_TX_NOTIFY" ] || [ -z "$MAIN_LOOP" ] ||
        [ "$BOOT_TX_NOTIFY" -le "$BDB_START" ] || [ "$BOOT_TX_NOTIFY" -ge "$MAIN_LOOP" ]; then
    echo "restored-network boot does not schedule TX-power restore after BDB_vStart" >&2
    exit 1
fi
BDB_TASK=$(grep -n 'bdb_taskBDB ( );' "$START" | cut -d: -f1)
TX_SERVICE=$(grep -n 'APP_vAHIServicePersistedTxPower();' "$START" | cut -d: -f1)
APP_EVENTS=$(grep -n 'APP_vHandleAppEvents ( );' "$START" | cut -d: -f1)
if [ -z "$BDB_TASK" ] || [ -z "$TX_SERVICE" ] || [ -z "$APP_EVENTS" ] ||
        [ "$TX_SERVICE" -le "$BDB_TASK" ] || [ "$TX_SERVICE" -ge "$APP_EVENTS" ]; then
    echo "TX-power restore is not serviced after bdb_taskBDB in the main loop" >&2
    exit 1
fi
if awk '
    /PUBLIC void APP_vAHIServicePersistedTxPower\(void\)/ { in_apply = 1 }
    in_apply && /\/\*\*\*        Local Functions/ { exit }
    in_apply && /PDM_eSaveRecordData|bAPP_AHISaveTxPower/ { found = 1 }
    END { exit found ? 0 : 1 }
' "$AHI"; then
    echo "network-start TX-power application must never write PDM" >&2
    exit 1
fi
awk '
    /PUBLIC void bdb_taskBDB\(void\)/ { in_task = 1 }
    in_task && /BDB_vNfStateMachine\(&sZpsAfEvent\)/ { state_machine = NR }
    in_task && /APP_vBdbCallback\(&sBDBEvent\)/ {
        forwarding = NR
        exit
    }
    END {
        if (!state_machine || !forwarding || state_machine >= forwarding)
            exit 1
    }
' "$BDB_STATE" || {
    echo "BDB no longer consumes formation state before application forwarding" >&2
    exit 1
}

# One cached restore load prevents recurring NWK_STARTED events from rereading
# flash. The second read site immediately verifies a successful SET-side save.
if [ "$(grep -c 'PDM_eReadDataFromRecord(PDM_ID_APP_TX_POWER' "$AHI")" -ne 2 ]; then
    echo "TX-power PDM must have one cached load and one save-verification read" >&2
    exit 1
fi
grep -A4 'bAPP_AHILoadStoredTxPower(&u8StoredTxPower)' "$AHI" \
    | grep -q 'u8StoredTxPower == u8TxPower'
grep -A6 'u8StoredTxPower == u8TxPower' "$AHI" | grep -q 'return TRUE'
grep -A9 'PDM_eSaveRecordData(PDM_ID_APP_TX_POWER' "$AHI" \
    | grep -q 'PDM_eReadDataFromRecord(PDM_ID_APP_TX_POWER'

# SET must read the old PIB before the short-circuited SET expression so every
# post-mutation failure has a rollback value.
grep -A3 'eAppApiPlmeGet(PHY_PIB_ATTR_TX_POWER, &u32OldTxPower)' "$AHI" \
    | grep -q '&&'
grep -A4 'eAppApiPlmeGet(PHY_PIB_ATTR_TX_POWER, &u32OldTxPower)' "$AHI" \
    | grep -q 'eAppApiPlmeSet(PHY_PIB_ATTR_TX_POWER, u8TxPower)'

# Production OCB images must not dispatch the unauthenticated legacy raw-PDM
# commands.  Development access is explicit, default-off, and the Makefile
# must reject the insecure/production combination before compilation.
grep -Eq '^INSECURE_DEV_RAW_PDM[[:space:]]*\?=[[:space:]]*0$' "$MAKEFILE"
grep -Eq '^OCB_TYPED_SUPPORT[[:space:]]*\?=[[:space:]]*1$' "$MAKEFILE"
grep -q 'ifdef INSECURE_DEV_RAW_PDM' "$ZNC_CMDS"
grep -q 'INSECURE_DEV_RAW_PDM cannot be enabled with production OCB_TYPED_SUPPORT' "$MAKEFILE"

RAW_PDM_CASES=$(grep -nE 'case E_SL_MSG_(DUMP_PDM_RECORD|RESTORE_PDM_RECORD_REQUEST|RESTORE_PDM_MODE)' "$ZNC_CMDS" | cut -d: -f1)
for line in $RAW_PDM_CASES; do
    awk -v target="$line" '
        NR > target { exit }
        /^[[:space:]]*#ifdef[[:space:]]+INSECURE_DEV_RAW_PDM[[:space:]]*$/ { gate = NR }
        /^[[:space:]]*#endif/ { gate = 0 }
        END { if (!gate) exit 1 }
    ' "$ZNC_CMDS" || {
        echo "raw PDM dispatch at $ZNC_CMDS:$line is not development-gated" >&2
        exit 1
    }
done

EXPECTED_EP1_OUTPUT='PRIVATE const uint16 s_au16Endpoint1OutputClusterList[25] = { 0x0000, 0x0001, 0x0012, 0x0019, 0x0300, 0x0004, 0x0003, 0x0008, 0x0006, 0x0005, 0x0101, 0x0702, 0x0500, 0x0201, 0x0204, 0x0400, 0x0401, 0x0403, 0x0406, 0x0405, 0x0402, 0x0b04, 0x0b05, 0x0102, 0x0502, };'
if [ -f "$ZPS_GEN" ]; then
    grep -Fqx "$EXPECTED_EP1_OUTPUT" "$ZPS_GEN"
    grep -A12 '^[[:space:]]*0x0104,$' "$ZPS_GEN" \
        | grep -A8 '^[[:space:]]*1,$' \
        | grep -q '^[[:space:]]*25,$'
    grep -A12 '^[[:space:]]*0x0104,$' "$ZPS_GEN" \
        | grep -q '^[[:space:]]*s_au16Endpoint1OutputClusterList,$'
else
    echo "generated ZPS checks skipped (run scripts/build.sh first)"
fi

# Prove the build-system invariant itself, rather than only grepping its text.
if make -s -C app/Build/ZigbeeNodeControlBridge \
        OCB_TYPED_SUPPORT=1 INSECURE_DEV_RAW_PDM=1 -n all >/dev/null 2>&1; then
    echo "Makefile accepted insecure raw PDM alongside production OCB" >&2
    exit 1
fi

grep -Eq '^OCB_KEY_EXPORT_RESTORE_EXPERIMENTAL[[:space:]]*\?=[[:space:]]*0$' "$MAKEFILE"
grep -q 'experimental OCB cannot be combined with INSECURE_DEV_RAW_PDM' "$MAKEFILE"
grep -q 'OCB_KEY_EXPORT_RESTORE_EXPERIMENTAL requires OCB_TYPED_SUPPORT' "$MAKEFILE"
grep -q 'APPSRC += ocb_experimental.c' "$MAKEFILE"
grep -q 'ocb_experimental.o' "$MAKEFILE"

for invalid in \
    'OCB_TYPED_SUPPORT=1 INSECURE_DEV_RAW_PDM=1 OCB_KEY_EXPORT_RESTORE_EXPERIMENTAL=1' \
    'OCB_TYPED_SUPPORT=0 INSECURE_DEV_RAW_PDM=0 OCB_KEY_EXPORT_RESTORE_EXPERIMENTAL=1'
do
    if make -s -C app/Build/ZigbeeNodeControlBridge $invalid -n all >/dev/null 2>&1; then
        echo "Makefile accepted invalid experimental OCB combination: $invalid" >&2
        exit 1
    fi
done

# Experimental capability is default-off and cannot set the reserved
# production-qualified BackupCapable bit.
grep -Eq '#define[[:space:]]+DIAG_CAP_BIT_OCB_EXPERIMENTAL_KEYS[[:space:]]+\(\(\(uint64\)1U\)[[:space:]]*<<[[:space:]]*16\)' "$HEADER"
grep -Eq '#define[[:space:]]+DIAG_CAP_BIT_OCB_BACKUP_QUALIFIED[[:space:]]+\(\(\(uint64\)1U\)[[:space:]]*<<[[:space:]]*17\)' "$HEADER"
grep -A1 -E '^#ifdef[[:space:]]+OCB_KEY_EXPORT_RESTORE_EXPERIMENTAL$' "$HEADER" \
    | grep -Eq '^#define[[:space:]]+DIAG_CAP_OCB_EXPERIMENTAL_BITMAP[[:space:]]+DIAG_CAP_BIT_OCB_EXPERIMENTAL_KEYS$'
if grep -Eq 'DIAG_CAP_BITMAP.*OCB_BACKUP_QUALIFIED|DIAG_CAP_OCB_EXPERIMENTAL_BITMAP.*OCB_BACKUP_QUALIFIED' "$HEADER"; then
    echo "unqualified OCB build advertises production BackupCapable" >&2
    exit 1
fi

# No reusable secret or false authentication claim: the confirmation is a
# public nonce/transaction/magic relation and the limitation bit is mandatory.
grep -q 'OCBEXP_CONFIRM_MAGIC' "$OCB_EXP_H"
grep -q 'The nonce confirmation is an accidental-invocation guard' "$OCB_EXP_H"
grep -q 'OCBEXP_LIMIT_NO_AUTH_OR_ENCRYPTION' "$OCB_EXP_H"
grep -q 'OCBEXP_LIMIT_FLASH_TCLK_COUNTERS' "$OCB_EXP_H"
grep -q 'vExpWipe(au8NwkKey' "$OCB_EXP_C"
grep -q 'vExpWipe(au8TcKey' "$OCB_EXP_C"
grep -q 'vExpWipe(&uFlashKey' "$OCB_EXP_C"

# Experimental streamed restore (0x0D24..0x0D28) is IMPLEMENTED but remains
# unqualified: the old non-mutating RestoreUnavailable stub must be gone, the
# real handlers must exist and be dispatched, and it must never set the reserved
# BackupCapable bit 17 (asserted below). The restore field carrier skips unknown
# field ids so a differently-versioned image cannot corrupt PDM layout.
if grep -q 'OCBEXP_vHandleRestoreUnavailable' "$OCB_EXP_C" "$OCB_EXP_H" "$ZNC_CMDS"; then
    echo "restore is implemented; the RestoreUnavailable stub must be removed" >&2
    exit 1
fi
grep -q 'OCBEXP_vHandleRestoreBegin' "$OCB_EXP_C"
grep -q 'OCBEXP_vHandleRestoreField' "$OCB_EXP_C"
grep -q 'OCBEXP_vHandleRestoreLink' "$OCB_EXP_C"
grep -q 'OCBEXP_vHandleValidate' "$OCB_EXP_C"
grep -q 'OCBEXP_vHandleCommit' "$OCB_EXP_C"
grep -q 'OCBEXP_FIELD_SKIPPED_UNKNOWN' "$OCB_EXP_C"
grep -q 'OCBEXP_vHandleCommit' "$ZNC_CMDS"
# COMMIT must emit its response frame BEFORE arming the deferred reboot, so the
# host always receives the result.
awk '
    /vSL_WriteMessage\(E_SL_MSG_OCBEXP_COMMIT_RSP/ { w = NR }
    /bResetIssued *= *TRUE/                        { if (!r) r = NR }
    END { exit (w && r && w < r) ? 0 : 1 }
' "$OCB_EXP_C" || {
    echo "COMMIT must reply before arming the reboot" >&2
    exit 1
}
# The adopted-IEEE override is applied at boot AFTER every ZPS_eAplAfInit()
# call (HIL root-caused: it hangs boot when the radio isn't clocked/initialised
# yet), gated by the experimental flag, and only from the dedicated PDM record.
grep -Eq '^#define[[:space:]]+PDM_ID_APP_OCB_ADOPT_IEEE[[:space:]]+0x12$' "$PDM_IDS"
awk '
    /^[[:space:]]*OCBEXP_vApplyAdoptedIeeeAtBoot[[:space:]]*\([[:space:]]*\)[[:space:]]*;/ { a = NR }
    /^[[:space:]]*ZPS_eAplAfInit[[:space:]]*\([[:space:]]*\)[[:space:]]*;/ { f = NR }
    END { exit (a && f && a > f) ? 0 : 1 }
' "$START" || {
    echo "adopted-IEEE override must be applied after every ZPS_eAplAfInit() call" >&2
    exit 1
}

# Publication docs must track the exact default gates, negotiation constants,
# every typed OCB opcode pair, and the explicit lack of restore qualification.
grep -q 'OCB_TYPED_SUPPORT=1' "$README"
grep -q 'OCB_KEY_EXPORT_RESTORE_EXPERIMENTAL=0' "$README"
grep -q 'INSECURE_DEV_RAW_PDM=0' "$README"
grep -qi '0x00000000000cc60f' "$OCB_DOC"
grep -qi 'DIAG_FW_BUILD_ID=0x010dc53d' "$OCB_DOC"
grep -q 'Reserved diagnostic bit 17' "$OCB_DOC"
grep -q '5 NO_SESSION' "$OCB_DOC"
grep -q 'RESTORE_FIELD' "$OCB_DOC"
for opcode in 18 19 1A 1B 1C 20 21 22 23 24 25 26 27 28 29 2A
do
    grep -qi "0x0D$opcode.*0x8D$opcode" "$OCB_DOC" || {
        echo "OCB documentation is missing opcode pair 0x0D$opcode/0x8D$opcode" >&2
        exit 1
    }
done
grep -q 'PDM_ID_APP_TX_POWER' "$MIGRATION"
grep -q 'native signed six-bit MiniMac codes' "$README"
grep -q 'native signed six-bit MiniMac codes' "$MIGRATION"
grep -q 'a704ba590c4a03e55be889d01c83b77c3491a09b708a8bca507b1829631b026e' "$README"
grep -q 'a704ba590c4a03e55be889d01c83b77c3491a09b708a8bca507b1829631b026e' "$MIGRATION"
grep -q 'a510aaba81c320a26a7de76428e7231fc91466bddd990087c104c157a41ce96b' "$README"
grep -q 'a510aaba81c320a26a7de76428e7231fc91466bddd990087c104c157a41ce96b' "$MIGRATION"
grep -q '5aeeaf1b91139bbae29f8f452db8f70fe921315cc378176610ec2027ecca6ab4' "$README"
grep -q '5aeeaf1b91139bbae29f8f452db8f70fe921315cc378176610ec2027ecca6ab4' "$MIGRATION"
grep -q 'f052390fa76e520fbcc51866d3e11c9cee60f33d9f80c31dd7358c751ccbf073' "$README"
grep -q 'f052390fa76e520fbcc51866d3e11c9cee60f33d9f80c31dd7358c751ccbf073' "$MIGRATION"
grep -q '4910a67aaeeb10fef7a5ad4e79065eeb2a1f3bc54677b342b94731ac525424fd' "$README"
grep -q '4910a67aaeeb10fef7a5ad4e79065eeb2a1f3bc54677b342b94731ac525424fd' "$MIGRATION"
grep -q '025e033cb73f8e68c57d41263c7b557888c49afa774c695aacce18dd053eecb4' "$README"
grep -q '025e033cb73f8e68c57d41263c7b557888c49afa774c695aacce18dd053eecb4' "$MIGRATION"
grep -q '196-byte linker' "$OCB_DOC"

# Exact generated v2395 legacy assumptions used for default-TC incoming
# counter indexing and table enumeration.
if [ -f "$ZPS_GEN" ]; then
    grep -Eq 's_keyPairTableStorage\[4\]' "$ZPS_GEN"
    grep -Eq 'au32IncomingFrameCounter\[4\]' "$ZPS_GEN"
    grep -Eq 's_keyPairTable = \{ s_keyPairTableStorage, 1 \}' "$ZPS_GEN"
    grep -Eq 's_asNwkSecMatSet\[2\]' "$ZPS_GEN"
    grep -Eq 's_asTrustCenterDeviceTable\[36\]' "$ZPS_GEN"
fi
grep -Eq 'psAplDefaultTCAPSLinkKey;|psAplDefaultTCAPSLinkKey' Components/ZPSAPL/Include/zps_apl_aib.h

# Typed OCB is additive, bounded, correlated, and cannot accidentally advertise
# key export or restore. The only key-by-EUI operation is an explicit
# unavailable response with a zero key length.
grep -Eq '#define[[:space:]]+E_SL_MSG_OCB_EXPORT_BEGIN_REQ[[:space:]]+\(0x0D18U\)' "$HEADER"
grep -Eq '#define[[:space:]]+E_SL_MSG_OCB_STATUS_RSP[[:space:]]+\(0x8D1CU\)' "$HEADER"
grep -Eq '#define[[:space:]]+OCB_CAP_BITMAP[[:space:]]+\(OCB_CAP_EXPORT_CORE \| OCB_CAP_STATUS_DIGEST\)' "$HEADER"
grep -A1 -E '^#ifdef[[:space:]]+OCB_TYPED_SUPPORT$' "$HEADER" \
    | grep -Eq '^#define[[:space:]]+DIAG_CAP_OCB_BITMAP[[:space:]]+DIAG_CAP_BIT_OCB_METADATA_EXPORT$'
grep -q 'CUSTOMDIAG_vHandleOcbExportBegin' "$DIAG"
grep -q 'CUSTOMDIAG_vHandleOcbExportCore' "$DIAG"
grep -q 'CUSTOMDIAG_vHandleOcbExportLinkKey' "$DIAG"
grep -q 'CUSTOMDIAG_vHandleOcbExportEnd' "$DIAG"
grep -q 'CUSTOMDIAG_vHandleOcbStatus' "$DIAG"
grep -q 'OCB_STATUS_FIELD_UNAVAILABLE' "$DIAG"
grep -Eq 'ZNC_BUF_U8_UPD\(&s_au8DiagTx\[u8Length\],[[:space:]]*0U,[[:space:]]*u8Length\); /\* no key bytes \*/' "$DIAG"
grep -q 'memset(&s_sOcbExport, 0, sizeof(s_sOcbExport))' "$DIAG"

if grep -Eq '#define[[:space:]]+OCB_CAP_BITMAP.*(OCB_CAP_LINK_KEYS|OCB_CAP_RESTORE)' "$HEADER"; then
    echo "export-only OCB must not advertise link-key or restore support" >&2
    exit 1
fi

grep -Eq '#define[[:space:]]+DIAG_PROTO_MAJOR[[:space:]]+\(1U\)' "$HEADER"
grep -Eq '#define[[:space:]]+DIAG_PROTO_MINOR[[:space:]]+\(2U\)' "$HEADER"
grep -Eq '#define[[:space:]]+DIAG_BUILD_REVISION[[:space:]]+\(17U\)' "$HEADER"
grep -Eq '#define[[:space:]]+DIAG_CAP_BIT_GP_COMMISSIONING[[:space:]]+\(\(\(uint64\)1U\)[[:space:]]*<<[[:space:]]*3\)' "$HEADER"
grep -Eq '#define[[:space:]]+DIAG_CAP_BIT_RESET_DIAGNOSTICS[[:space:]]+\(\(\(uint64\)1U\)[[:space:]]*<<[[:space:]]*18\)' "$HEADER"
grep -Eq '#define[[:space:]]+DIAG_CAP_BIT_RESET_CONTEXT[[:space:]]+\(\(\(uint64\)1U\)[[:space:]]*<<[[:space:]]*19\)' "$HEADER"
grep -q '| DIAG_CAP_BIT_RESET_DIAGNOSTICS' "$HEADER"
grep -q '| DIAG_CAP_BIT_RESET_CONTEXT' "$HEADER"
grep -Eq 'E_SL_MSG_MANUFACTURER_CODE_REQ[[:space:]]*=[[:space:]]*0x0D16' "$SERIAL"
grep -Eq 'E_SL_MSG_MANUFACTURER_CODE_RSP[[:space:]]*=[[:space:]]*0x8D16' "$SERIAL"
grep -Eq 'E_SL_MSG_GP_COMMISSION_REQ[[:space:]]*=[[:space:]]*0x0D17' "$SERIAL"
grep -Eq 'E_SL_MSG_GP_COMMISSION_RSP[[:space:]]*=[[:space:]]*0x8D17' "$SERIAL"
grep -Eq 'E_SL_MSG_RESET_DIAG_REQ[[:space:]]*=[[:space:]]*0x0D2B' "$SERIAL"
grep -Eq 'E_SL_MSG_RESET_DIAG_RSP[[:space:]]*=[[:space:]]*0x8D2B' "$SERIAL"
grep -Eq 'E_SL_MSG_RESET_CONTEXT_REQ[[:space:]]*=[[:space:]]*0x0D2C' "$SERIAL"
grep -Eq 'E_SL_MSG_RESET_CONTEXT_RSP[[:space:]]*=[[:space:]]*0x8D2C' "$SERIAL"
grep -q 'CUSTOMDIAG_vHandleResetDiag' "$DIAG"
grep -q 'CUSTOMDIAG_vHandleResetDiag' "$ZNC_CMDS"
grep -q 'CUSTOMDIAG_vHandleResetContext' "$DIAG"
grep -q 'CUSTOMDIAG_vHandleResetContext' "$ZNC_CMDS"
grep -Eq '^#define[[:space:]]+DIAG_RESET_DIAG_RSP_LEN[[:space:]]+\(6U\)' "$HEADER"
grep -Eq '^#define[[:space:]]+DIAG_RESET_CONTEXT_RSP_LEN[[:space:]]+\(22U\)' "$HEADER"
grep -q 'require bit 18 before sending `0x0D2B`' "$RESET_DOC"
grep -q 'independently require bit 19' "$RESET_DOC"
grep -q 's_u16BootPowerStatus = u16AHI_PowerStatus();' "$DIAG"
grep -q 'bAHI_WatchdogResetEvent()' "$DIAG"
grep -q 'bAHI_BrownOutEventResetStatus()' "$DIAG"
grep -q 'section(".noinit")' "$DIAG"
grep -q 's_u8BootResetReason' "$DIAG"
grep -q '.noinit ALIGN (0x04) (NOLOAD)' Chip/JN5169/Build/AppBuildEnd.ld
grep -q 'DIAG_RESET_REASON_STACK_OVERFLOW' "$START"
grep -q 'CUSTOMDIAG_vRetainExceptionContext' "$START"
grep -q 'l.mfspr %0,r0,0x0020' "$START"
grep -q 'l.mfspr %0,r0,0x0030' "$START"
if grep -q 'while ( ( uint32 ) pu32Stack & 0x7fff )' "$START"; then
    echo "exception handler still dereferences a post-fault stack walk" >&2
    exit 1
fi
grep -q 'CUSTOMDIAG_vRetainResetReason(u8ResetReason);' "$ZNC_CMDS"
grep -q 'CUSTOMDIAG_vCaptureResetCause();' "$START"
grep -q 'RESET_DIAGNOSTIC_ABI.md' "$README"

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

# SerialLink TX treats the payload as immutable and streams a virtual trailing
# LQI through both checksum and escaping. Exact-size scalar and 8-byte status
# callers therefore need no hidden byte of writable storage.
grep -Eq 'PUBLIC void vSL_WriteMessage\(uint16 u16Type,' "$SERIAL_C"
grep -Eq 'const uint8 \*pu8Data' "$SERIAL_C"
grep -q 'u8SL_CalculateCRCWithLqi' "$SERIAL_C"
grep -Eq 'vSL_TxByte\(FALSE,[[:space:]]*u8LinkQuality\);' "$SERIAL_C"
if grep -Eq 'pu8Data\[[^]]+\][[:space:]]*=' "$SERIAL_C"; then
    echo "vSL_WriteMessage still mutates caller payload" >&2
    exit 1
fi
grep -Eq 'uint8 au8Status\[8\];' "$DIAG"
if grep -q 'LQI_RESERVE' "$HEADER" "$DIAG" "$OCB_EXP_C"; then
    echo "obsolete caller-side LQI reserve remains" >&2
    exit 1
fi

# Golden framing equivalence for the default XOR checksum and optional CCITT
# checksum. The first vector exercises escaping in payload and LQI; the second
# is an exact-size scalar caller. Both models must produce the historical wire
# bytes while leaving the supplied bytearray unchanged.
python3 -c '
def esc(value):
    return bytes((2, value ^ 0x10)) if value < 0x10 else bytes((value,))

def crc8_update(crc, value):
    crc ^= value
    for _ in range(8):
        crc = (((crc << 1) ^ 0x07) & 0xff) if crc & 0x80 else ((crc << 1) & 0xff)
    return crc

def frame_legacy(msg_type, payload, lqi, ccitt):
    data = bytes(payload) + bytes((lqi,))
    length = len(data)
    crc_input = (msg_type & 0xff, msg_type >> 8, length & 0xff, length >> 8) + tuple(data)
    crc = 0
    for value in crc_input:
        crc = crc8_update(crc, value) if ccitt else crc ^ value
    body = (msg_type >> 8, msg_type & 0xff, length >> 8, length & 0xff, crc) + tuple(data)
    return bytes((1,)) + b"".join(esc(value) for value in body) + bytes((3,))

def frame_streamed(msg_type, payload, lqi, ccitt):
    before = bytes(payload)
    length = len(payload) + 1
    crc_input = (msg_type & 0xff, msg_type >> 8, length & 0xff, length >> 8)
    crc = 0
    for value in crc_input + tuple(payload) + (lqi,):
        crc = crc8_update(crc, value) if ccitt else crc ^ value
    body = (msg_type >> 8, msg_type & 0xff, length >> 8, length & 0xff, crc)
    wire = bytes((1,)) + b"".join(esc(value) for value in body)
    wire += b"".join(esc(value) for value in payload)
    wire += esc(lqi) + bytes((3,))
    assert bytes(payload) == before
    return wire

vectors = (
    (0x8002, bytearray((0, 1, 2, 3, 0x10)), 0x02,
     "018002120210021696021002110212021310021203",
     "018002120210021624021002110212021310021203"),
    (0x807a, bytearray((1,)), 0,
     "01807a02100212f90211021003",
     "01807a02100212600211021003"),
)
for msg_type, payload, lqi, xor_golden, ccitt_golden in vectors:
    for ccitt, golden in ((False, xor_golden), (True, ccitt_golden)):
        old = frame_legacy(msg_type, payload, lqi, ccitt)
        new = frame_streamed(msg_type, payload, lqi, ccitt)
        assert old == new == bytes.fromhex(golden)
'

# RAW_MODE_ON endpoint-0 traffic must branch before the stock ZDP unpacker.
# Valid Device_annce remains the sole typed exception for stock 0x004D.
awk '
    /u8RawMode == RAW_MODE_ON &&/ && !raw_guard { raw_guard = NR }
    /zps_bAplZdpUnpackResponse/ && !unpack       { unpack = NR }
    END { if (!raw_guard || !unpack || raw_guard >= unpack) exit 1 }
' "$GENERAL_EVENTS" || {
    echo "raw endpoint-0 guard does not precede ZDP unpacking" >&2
    exit 1
}
grep -q 'APP_ZDP_DEVICE_ANNCE_PAYLOAD_LEN' "$GENERAL_EVENTS"
grep -A16 'u8RawMode == RAW_MODE_ON &&' "$GENERAL_EVENTS" \
    | grep -q 'PDUM_eAPduFreeAPduInstance'
grep -A18 'u8RawMode == RAW_MODE_ON &&' "$GENERAL_EVENTS" \
    | grep -q 'return;'

# Malformed 0x0530 requests are rejected before APP_eApsProfileDataRequest can
# allocate/copy an APDU. Length subtraction occurs only after the fixed header
# has been proven present.
grep -q 'APP_RAW_APS_SHORT_HEADER_LEN' "$ZNC_CMDS"
grep -q 'APP_RAW_APS_IEEE_HEADER_LEN' "$ZNC_CMDS"
grep -A55 'case (E_SL_MSG_SEND_RAW_APS_DATA_PACKET):' "$ZNC_CMDS" \
    | grep -q 'u16PacketLength < u16HeaderLength'
grep -A60 'case (E_SL_MSG_SEND_RAW_APS_DATA_PACKET):' "$ZNC_CMDS" \
    | grep -q 'u16PacketLength - u16HeaderLength'

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

# Endpoint-1 output descriptor advertised by the NCP. rev17 disassembly proved
# that raw 0x0530 unicast validates the source endpoint but does not search
# this list; keep it complete for descriptor truth and interoperability, not
# as an explanation for every APS 0xA3. These entries are const/flash
# descriptor data and must NOT be paired with new ZCL runtime instances.
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

# The output declarations must stay descriptor-only: exactly three ZCL client/server
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

grep -Eq '^override LDFLAGS \+= -Wl,--wrap=zps_psFindKeyDescr$' "$MAKEFILE"
grep -Eq '^DISABLE_LTO \?= 1$' "$MAKEFILE"
grep -q 'zps_psFindKeyDescr guard requires DISABLE_LTO=1' "$MAKEFILE"
grep -Eq '^APPSRC \+= zps_key_index_guard\.c$' "$MAKEFILE"
grep -q '__real_zps_psFindKeyDescr(pvApl, u64DeviceAddr, pu32Index)' "$APS_KEY_GUARD"
grep -q 'if (psKey == NULL && pu32Index != NULL)' "$APS_KEY_GUARD"
grep -q '\*pu32Index = 0U;' "$APS_KEY_GUARD"
grep -q 'return psKey;' "$APS_KEY_GUARD"
if grep -Eq '(^|[^_[:alnum:]])zps_psFindKeyDescr[[:space:]]*\(' "$APS_KEY_GUARD"; then
    echo "APS key-index guard calls the wrapped name directly" >&2
    exit 1
fi
GUARD_IF=$(grep -n 'if (psKey == NULL && pu32Index != NULL)' "$APS_KEY_GUARD" | cut -d: -f1)
GUARD_STORE=$(grep -n '\*pu32Index = 0U;' "$APS_KEY_GUARD" | cut -d: -f1)
GUARD_RETURN=$(grep -n 'return psKey;' "$APS_KEY_GUARD" | cut -d: -f1)
if [ -z "$GUARD_IF" ] || [ -z "$GUARD_STORE" ] || [ -z "$GUARD_RETURN" ] ||
        [ "$GUARD_STORE" -le "$GUARD_IF" ] || [ "$GUARD_STORE" -ge "$GUARD_RETURN" ]; then
    echo "APS key-index store is not contained in the NULL-result guard" >&2
    exit 1
fi
if [ "$(grep -Ec -- '--wrap([=,]|[[:space:]])' "$MAKEFILE")" -ne 1 ]; then
    echo "unapproved linker interposition is enabled" >&2
    exit 1
fi
if [ -f "$ELF" ]; then
    if [ ! -x "$BA2_BIN/ba-elf-nm" ] || [ ! -x "$BA2_BIN/ba-elf-objdump" ]; then
        echo "built ELF exists but BA2 inspection tools are unavailable" >&2
        exit 1
    fi
    "$BA2_BIN/ba-elf-nm" "$ELF" | grep -q ' T __wrap_zps_psFindKeyDescr$'
    "$BA2_BIN/ba-elf-nm" "$ELF" | grep -q ' T zps_psFindKeyDescr$'
    "$BA2_BIN/ba-elf-nm" -S "$ELF" \
        | grep -Eq '^[0-9a-fA-F]+[[:space:]]+00000032[[:space:]]+r[[:space:]]+s_au16Endpoint1OutputClusterList$'
    "$BA2_BIN/ba-elf-objdump" -d "$ELF" \
        | grep -A220 '<bDuplicateCheck>:' \
        | grep -q '<__wrap_zps_psFindKeyDescr>'
fi

if grep -Eq '^[[:space:]]*APP_MigratePDM[[:space:]]*\(' "$START"; then
    echo "unsafe legacy PDM migration is enabled" >&2
    exit 1
fi

echo "source and ABI checks passed"
