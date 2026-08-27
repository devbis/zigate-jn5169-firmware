/****************************************************************************
 *
 * MODULE:  custom_diag.c
 *
 * DESCRIPTION:
 *   Implementation of the compact versioned read-only UART diagnostic
 *   extension declared in custom_diag.h. See that header for the safety
 *   envelope. Nothing here mutates persistent network identity, exposes key
 *   material, or performs raw PDM / credential-flash access.
 *
 *   Synchronisation note: the local neighbour and route tables are read using
 *   the same lock-free, copy-then-serialise strategy already used by the stock
 *   0x0015 address-map handler (which reads psNtActv and resolves the mapped
 *   IEEE address with no mutex). APP_vProcessIncomingSerialCommands() and the
 *   ZPS stack run cooperatively in the same application task, so each table
 *   entry is snapshotted into locals immediately before serialisation to
 *   minimise the chance of a torn read. No ZPS mutex is held across a
 *   vSL_WriteMessage() call.
 *
 ****************************************************************************/

/****************************************************************************/
/***        Include files                                                 ***/
/****************************************************************************/

#include <jendefs.h>
#include <string.h>
#include "SerialLink.h"
#include "app_common.h"
#include "AppApi.h"
#include "AppHardwareApi.h"
#include "PDM.h"
#include "pdum_apl.h"
#include "pdum_nwk.h"
#include "zps_apl.h"
#include "zps_apl_zdo.h"
#include "zps_apl_aib.h"
#include "zps_apl_af.h"
#include "zps_nwk_nib.h"
#include "zps_nwk_sec.h"
#include "mac_sap.h"
#include "bdb_api.h"
#include "tclk_diagnostic.h"
#include "custom_diag.h"
#include "zcl_options.h"   /* ZCL_MANUFACTURER_CODE (baked-in default) */

/****************************************************************************/
/***        External application state                                    ***/
/****************************************************************************/

extern tsBDB       sBDB;        /* BDB attributes, incl. bbdbNodeIsOnANetwork */
extern tsZllState  sZllState;   /* Application device/node state              */

/****************************************************************************/
/***        Local shared response buffer                                  ***/
/***                                                                       ***/
/*** Single static buffer reused by every handler. The extra LQI byte      ***/
/*** appended by vSL_WriteMessage() is covered by DIAG_TX_LQI_RESERVE.     ***/
/*** Safe because serial commands are dispatched sequentially from a single***/
/*** application-task context with no handler re-entrancy.                 ***/
/****************************************************************************/

PRIVATE uint8 s_au8DiagTx[DIAG_TX_BUFFER_SIZE];

/****************************************************************************/
/***        Local helpers                                                 ***/
/****************************************************************************/

/* Emit the stock 7-byte E_SL_MSG_STATUS (0x8000) frame, matching the exact
 * field order used by every other command in app_Znc_cmds.c. */
PRIVATE void vDiagSendStatus(uint16 u16PacketType, uint8 u8Status)
{
    uint8 au8Status[8];   /* 7 payload bytes + 1 reserved LQI byte */
    uint8 u8Length = 0;

    ZNC_BUF_U8_UPD  ( &au8Status[ u8Length ], u8Status,             u8Length );
    ZNC_BUF_U8_UPD  ( &au8Status[ u8Length ], 0,                    u8Length ); /* seq num      */
    ZNC_BUF_U16_UPD ( &au8Status[ u8Length ], u16PacketType,        u8Length );
    ZNC_BUF_U8_UPD  ( &au8Status[ u8Length ], 0,                    u8Length ); /* request sent */
    ZNC_BUF_U8_UPD  ( &au8Status[ u8Length ], 0,                    u8Length ); /* aps seq num  */
    ZNC_BUF_U8_UPD  ( &au8Status[ u8Length ], PDUM_u8GetNpduUse(),  u8Length );
    ZNC_BUF_U8_UPD  ( &au8Status[ u8Length ], u8GetApduUse(),       u8Length );

    vSL_WriteMessage(E_SL_MSG_STATUS, u8Length, au8Status, 0);
}

/* Return the full TX power PIB byte as returned by the PHY, or DIAG_U8_NA on
 * failure. The full byte is preserved (including the tolerance bit 0x40 that
 * MiniMac reports); the masked 6-bit level and signed code are derived
 * separately below. */
PRIVATE uint8 u8DiagTxPowerRaw(void)
{
    uint32 u32TxPower = 0;

    if (eAppApiPlmeGet(PHY_PIB_ATTR_TX_POWER, &u32TxPower) == PHY_ENUM_SUCCESS)
    {
        return (uint8)(u32TxPower & 0xFFU);
    }
    return DIAG_U8_NA;
}

/* Masked 6-bit TX power level (raw & PHY_PIB_TX_POWER_MASK). */
PRIVATE uint8 u8DiagTxPowerLevel(uint8 u8Raw)
{
    if (u8Raw == DIAG_U8_NA) { return DIAG_U8_NA; }
    return (uint8)(u8Raw & PHY_PIB_TX_POWER_MASK);
}

/* Six-bit signed power code interpretation of the masked level:
 * (level & 0x20) ? level - 64 : level. This is the raw signed six-bit code,
 * NOT an exact radiated/effective dBm figure (the closed PHY exposes no exact
 * raw->dBm table). */
PRIVATE int8 i8DiagTxPowerSignedCode(uint8 u8Level)
{
    if (u8Level == DIAG_U8_NA) { return (int8)0x80; }  /* NA sentinel */
    if (u8Level & 0x20U)       { return (int8)((int)u8Level - 64); }
    return (int8)u8Level;
}

/* Count used neighbour-table entries (address below the broadcast sentinels). */
PRIVATE void vDiagNeighbourUsage(ZPS_tsNwkNib *psNib, uint8 *pu8Used, uint8 *pu8Total)
{
    uint16 i;
    uint16 u16Total = psNib->sTblSize.u16NtActv;
    uint16 u16Used  = 0;

    for (i = 0; i < u16Total; i++)
    {
        if (psNib->sTbl.psNtActv[i].uAncAttrs.bfBitfields.u1Used &&
            psNib->sTbl.psNtActv[i].u16NwkAddr < 0xFFFEU)
        {
            u16Used++;
        }
    }
    *pu8Used  = (u16Used  > 0xFFU) ? 0xFFU : (uint8)u16Used;
    *pu8Total = (u16Total > 0xFFU) ? 0xFFU : (uint8)u16Total;
}

/* Count active route-table entries (status field non-idle). */
PRIVATE void vDiagRouteUsage(ZPS_tsNwkNib *psNib, uint8 *pu8Used, uint8 *pu8Total)
{
    uint16 i;
    uint16 u16Total = psNib->sTblSize.u16Rt;
    uint16 u16Used  = 0;

    for (i = 0; i < u16Total; i++)
    {
        if (psNib->sTbl.psRt[i].u16NwkDstAddr < 0xFFFEU)
        {
            u16Used++;
        }
    }
    *pu8Used  = (u16Used  > 0xFFU) ? 0xFFU : (uint8)u16Used;
    *pu8Total = (u16Total > 0xFFU) ? 0xFFU : (uint8)u16Total;
}

/* Return TRUE if the group entry references at least one endpoint. */
PRIVATE bool_t bDiagGroupEntryUsed(const ZPS_tsAplApsmeGroupTableEntry *psEntry)
{
    uint8 i;

    if (psEntry->u16Groupid == 0U)
    {
        return FALSE;
    }
    for (i = 0; i < sizeof(psEntry->au8Endpoint); i++)
    {
        if (psEntry->au8Endpoint[i] != 0U)
        {
            return TRUE;
        }
    }
    return FALSE;
}

PRIVATE void vDiagGroupUsage(ZPS_tsAplApsmeAIBGroupTable *psGroup,
                             uint8 *pu8Used, uint8 *pu8Total)
{
    uint32 i;
    uint32 u32Total = (psGroup != NULL) ? psGroup->u32SizeOfGroupTable : 0U;
    uint32 u32Used  = 0;

    for (i = 0; i < u32Total; i++)
    {
        if (bDiagGroupEntryUsed(&psGroup->psAplApsmeGroupTableId[i]))
        {
            u32Used++;
        }
    }
    *pu8Used  = (u32Used  > 0xFFU) ? 0xFFU : (uint8)u32Used;
    *pu8Total = (u32Total > 0xFFU) ? 0xFFU : (uint8)u32Total;
}

/****************************************************************************/
/***        Capability negotiation (0x0D0F / 0x8D0F)                      ***/
/****************************************************************************/

PUBLIC void CUSTOMDIAG_vHandleCapability(uint16 u16Len, const uint8 *pu8Rx)
{
    uint8  u8Status = E_SL_MSG_STATUS_SUCCESS;
    uint32 u32Nonce = 0;
    uint8  u8Length = 0;

    /* Strict fixed-length + magic validation. */
    if (u16Len != DIAG_CAP_REQ_LEN ||
        pu8Rx[0] != DIAG_CAP_MAGIC_0 || pu8Rx[1] != DIAG_CAP_MAGIC_1 ||
        pu8Rx[2] != DIAG_CAP_MAGIC_2 || pu8Rx[3] != DIAG_CAP_MAGIC_3)
    {
        u8Status = E_SL_MSG_STATUS_INCORRECT_PARAMETERS;
    }

    vDiagSendStatus(E_SL_MSG_CAPABILITY_REQ, u8Status);
    if (u8Status != E_SL_MSG_STATUS_SUCCESS)
    {
        return;
    }

    /* Host major/minor at [4]/[5] are accepted for logging only; the nonce is
     * echoed verbatim so the host can match request and response. */
    u32Nonce = ZNC_RTN_U32(pu8Rx, 6);

    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], DIAG_CAP_MAGIC_0,   u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], DIAG_CAP_MAGIC_1,   u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], DIAG_CAP_MAGIC_2,   u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], DIAG_CAP_MAGIC_3,   u8Length );
    ZNC_BUF_U32_UPD ( &s_au8DiagTx[ u8Length ], u32Nonce,           u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], DIAG_PROTO_MAJOR,   u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], DIAG_PROTO_MINOR,   u8Length );
    ZNC_BUF_U32_UPD ( &s_au8DiagTx[ u8Length ], DIAG_FW_BUILD_ID,   u8Length );
    ZNC_BUF_U64_UPD ( &s_au8DiagTx[ u8Length ], DIAG_CAP_BITMAP,    u8Length );
    ZNC_BUF_U16_UPD ( &s_au8DiagTx[ u8Length ], DIAG_TX_PAYLOAD_MAX, u8Length );

    vSL_WriteMessage(E_SL_MSG_CAPABILITY_RSP, u8Length, s_au8DiagTx, 0);
}

/****************************************************************************/
/***        General read-only diagnostics (0x0D1F / 0x8D1F)               ***/
/****************************************************************************/

PUBLIC void CUSTOMDIAG_vHandleGeneralDiag(uint16 u16Len)
{
    uint8         u8Length = 0;
    void         *pvNwk;
    ZPS_tsNwkNib *psNib;
    ZPS_tsAplApsmeAIBGroupTable *psGroup;
    uint32        u32Channel = 0;
    uint8         u8NbUsed = 0, u8NbTotal = 0;
    uint8         u8RtUsed = 0, u8RtTotal = 0;
    uint8         u8GrUsed = 0, u8GrTotal = 0;
    uint8         u8TxRaw;
    uint8         u8TxLevel;
    uint8         u8DiagFlags = 0;
    uint8         au8Tclk[sizeof(TCLKDIAG_tsState)];
    uint8         u8TclkCb, u8TclkAddRepl, u8TclkCred;

    if (u16Len != 0)
    {
        vDiagSendStatus(E_SL_MSG_GENERAL_DIAG_REQ, E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
        return;
    }

    vDiagSendStatus(E_SL_MSG_GENERAL_DIAG_REQ, E_SL_MSG_STATUS_SUCCESS);

    pvNwk = ZPS_pvAplZdoGetNwkHandle();
    psNib = ZPS_psNwkNibGetHandle(pvNwk);
    psGroup = ZPS_psAplAibGetAib()->psAplApsmeGroupTable;

    (void)eAppApiPlmeGet(PHY_PIB_ATTR_CURRENT_CHANNEL, &u32Channel);
    vDiagNeighbourUsage(psNib, &u8NbUsed, &u8NbTotal);
    vDiagRouteUsage(psNib, &u8RtUsed, &u8RtTotal);
    vDiagGroupUsage(psGroup, &u8GrUsed, &u8GrTotal);
    u8TxRaw = u8DiagTxPowerRaw();
    u8TxLevel = u8DiagTxPowerLevel(u8TxRaw);

    /* TCLK summary via the same odd/even snapshot protocol used by 0x0D00.
     * If no stable snapshot is available, flag it and emit NA sentinels rather
     * than reading the mid-update volatile state directly. Offsets match the
     * fixed TCLKDIAG_tsState ABI (0x18 status, 0x1b add/replace, 0x1c cred). */
    if (TCLKDIAG_bSnapshot(au8Tclk))
    {
        u8TclkCb      = au8Tclk[0x18];
        u8TclkAddRepl = au8Tclk[0x1b];
        u8TclkCred    = au8Tclk[0x1c];
    }
    else
    {
        u8DiagFlags  |= DIAG_GENDIAG_FLAG_TCLK_UNAVAILABLE;
        u8TclkCb      = DIAG_U8_NA;
        u8TclkAddRepl = DIAG_U8_NA;
        u8TclkCred    = DIAG_U8_NA;
    }

    /* Header */
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], DIAG_RSP_VERSION,  u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], E_SL_MSG_STATUS_SUCCESS, u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8DiagFlags,       u8Length );

    /* Device / BDB state */
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], (uint8)ZPS_eAplZdoGetDeviceType(), u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], (uint8)sZllState.eNodeState,       u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], (uint8)sBDB.sAttrib.bbdbNodeIsOnANetwork, u8Length );

    /* Network identity */
    ZNC_BUF_U16_UPD ( &s_au8DiagTx[ u8Length ], ZPS_u16NwkNibGetNwkAddr(pvNwk),  u8Length );
    ZNC_BUF_U64_UPD ( &s_au8DiagTx[ u8Length ], ZPS_u64NwkNibGetExtAddr(pvNwk),  u8Length );
    ZNC_BUF_U16_UPD ( &s_au8DiagTx[ u8Length ], ZPS_u16NwkNibGetMacPanId(pvNwk), u8Length );
    ZNC_BUF_U64_UPD ( &s_au8DiagTx[ u8Length ], ZPS_u64NwkNibGetEpid(pvNwk),     u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], (uint8)u32Channel,               u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], psNib->sPersist.u8UpdateId,      u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], (uint8)ZPS_bGetPermitJoiningStatus(), u8Length );

    /* Security metadata (no key bytes) */
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], (uint8)ZPS_bNwkSecHaveNetworkKey(pvNwk), u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], psNib->sPersist.u8ActiveKeySeqNumber,   u8Length );

    /* Table occupancy */
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8NbUsed,  u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8NbTotal, u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8RtUsed,  u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8RtTotal, u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8GrUsed,  u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8GrTotal, u8Length );

    /* Buffer / packet engine occupancy */
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], PDUM_u8GetNpduUse(), u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8GetApduUse(),      u8Length );

    /* TX power: full PIB byte, masked 6-bit level, six-bit signed power code.
     * The signed code is NOT an exact radiated dBm value. */
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8TxRaw,     u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8TxLevel,   u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], (uint8)i8DiagTxPowerSignedCode(u8TxLevel), u8Length );

    /* PDM occupancy / wear (safe, read-only) */
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], PDM_u8GetSegmentCapacity(),  u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], PDM_u8GetSegmentOccupancy(), u8Length );

    /* TCLK diagnostic summary (statuses only, never key bytes) */
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8TclkCb,      u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8TclkAddRepl, u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8TclkCred,    u8Length );

    vSL_WriteMessage(E_SL_MSG_GENERAL_DIAG_RSP, u8Length, s_au8DiagTx, 0);
}

/****************************************************************************/
/***        Paginated local neighbour table (0x0D14 / 0x8D14)            ***/
/****************************************************************************/

PUBLIC void CUSTOMDIAG_vHandleNeighbours(uint16 u16Len, const uint8 *pu8Rx)
{
    uint8         u8ReqVersion;
    uint8         u8Start;
    uint8         u8Max;
    uint8         u8Flags;
    uint8         u8Length = 0;
    uint8         u8Returned = 0;
    uint16        u16Total;
    uint16        i;
    void         *pvNwk;
    ZPS_tsNwkNib *psNib;

    /* Strict fixed-length + bounds validation. */
    if (u16Len != DIAG_NEIGHBOUR_REQ_LEN)
    {
        vDiagSendStatus(E_SL_MSG_LOCAL_NEIGHBOUR_REQ, E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
        return;
    }
    u8ReqVersion = pu8Rx[0];
    u8Start      = pu8Rx[1];
    u8Max        = pu8Rx[2];
    u8Flags      = pu8Rx[3];

    if (u8ReqVersion != DIAG_REQ_VERSION ||
        u8Max == 0U || u8Max > DIAG_NEIGHBOUR_MAX_RECORDS ||
        (u8Flags & ~DIAG_NEIGHBOUR_FLAG_MASK) != 0U)
    {
        vDiagSendStatus(E_SL_MSG_LOCAL_NEIGHBOUR_REQ, E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
        return;
    }

    vDiagSendStatus(E_SL_MSG_LOCAL_NEIGHBOUR_REQ, E_SL_MSG_STATUS_SUCCESS);

    pvNwk = ZPS_pvAplZdoGetNwkHandle();
    psNib = ZPS_psNwkNibGetHandle(pvNwk);
    u16Total = psNib->sTblSize.u16NtActv;

    /* Reserve prefix space; fill records first, then back-fill the prefix. */
    u8Length = DIAG_PAGE_PREFIX_LEN;

    for (i = u8Start; i < u16Total && u8Returned < u8Max; i++)
    {
        /* Per-entry snapshot into locals (see file-level synchronisation note). */
        ZPS_tsNwkActvNtEntry sEntry = psNib->sTbl.psNtActv[i];
        uint64 u64Ieee;
        uint8  u8Used = (uint8)sEntry.uAncAttrs.bfBitfields.u1Used;

        if (u8Used == 0U &&
            (u8Flags & DIAG_NEIGHBOUR_FLAG_INCLUDE_UNUSED) == 0U)
        {
            continue;
        }

        /* Only resolve the mapped IEEE address when the entry is used and its
         * lookup index is within the address-map bounds; otherwise the mapped
         * IEEE is meaningless, so emit the all-FF unavailable sentinel. */
        if (u8Used != 0U && sEntry.u16Lookup < psNib->sTblSize.u16AddrMap)
        {
            u64Ieee = ZPS_u64NwkNibGetMappedIeeeAddr(pvNwk, sEntry.u16Lookup);
        }
        else
        {
            u64Ieee = DIAG_IEEE_NA;
        }

        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], (uint8)i,               u8Length );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8Used,                 u8Length );
        ZNC_BUF_U16_UPD ( &s_au8DiagTx[ u8Length ], sEntry.u16NwkAddr,      u8Length );
        ZNC_BUF_U64_UPD ( &s_au8DiagTx[ u8Length ], u64Ieee,                u8Length );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], (uint8)sEntry.uAncAttrs.bfBitfields.u2Relationship, u8Length );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], (uint8)sEntry.uAncAttrs.bfBitfields.u1DeviceType,   u8Length );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], (uint8)sEntry.uAncAttrs.bfBitfields.u1RxOnWhenIdle, u8Length );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], (uint8)sEntry.uAncAttrs.bfBitfields.u1Authenticated,u8Length );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], sEntry.u8LinkQuality,   u8Length );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], sEntry.u8Age,           u8Length );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], sEntry.u8TxFailed,      u8Length );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], (uint8)sEntry.uAncAttrs.bfBitfields.u3OutgoingCost, u8Length );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], (uint8)sEntry.i8TXPower, u8Length );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], sEntry.u8MacID,         u8Length );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], sEntry.u8ZedTimeoutindex, u8Length );

        u8Returned++;
    }

    /* Back-fill the common prefix, including the resume cursor (i == index one
     * past the last slot scanned; equals total when the scan reached the end). */
    {
        uint8 u8Prefix = 0;
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Prefix ], DIAG_RSP_VERSION,        u8Prefix );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Prefix ], E_SL_MSG_STATUS_SUCCESS, u8Prefix );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Prefix ], u8Flags,                 u8Prefix );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Prefix ], u8Start,                 u8Prefix );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Prefix ], (uint8)((u16Total > 0xFFU) ? 0xFFU : u16Total), u8Prefix );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Prefix ], u8Returned,              u8Prefix );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Prefix ], (uint8)((i > 0xFFU) ? 0xFFU : i), u8Prefix );
    }

    vSL_WriteMessage(E_SL_MSG_LOCAL_NEIGHBOUR_RSP, u8Length, s_au8DiagTx, 0);
}

/****************************************************************************/
/***        Paginated local route table (0x0D15 / 0x8D15)                ***/
/****************************************************************************/

PUBLIC void CUSTOMDIAG_vHandleRoutes(uint16 u16Len, const uint8 *pu8Rx)
{
    uint8         u8ReqVersion;
    uint8         u8Start;
    uint8         u8Max;
    uint8         u8Flags;
    uint8         u8Length = 0;
    uint8         u8Returned = 0;
    uint16        u16Total;
    uint16        i;
    void         *pvNwk;
    ZPS_tsNwkNib *psNib;

    if (u16Len != DIAG_ROUTE_REQ_LEN)
    {
        vDiagSendStatus(E_SL_MSG_LOCAL_ROUTE_REQ, E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
        return;
    }
    u8ReqVersion = pu8Rx[0];
    u8Start      = pu8Rx[1];
    u8Max        = pu8Rx[2];
    u8Flags      = pu8Rx[3];

    if (u8ReqVersion != DIAG_REQ_VERSION ||
        u8Max == 0U || u8Max > DIAG_ROUTE_MAX_RECORDS ||
        (u8Flags & ~DIAG_ROUTE_FLAG_MASK) != 0U)
    {
        vDiagSendStatus(E_SL_MSG_LOCAL_ROUTE_REQ, E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
        return;
    }

    vDiagSendStatus(E_SL_MSG_LOCAL_ROUTE_REQ, E_SL_MSG_STATUS_SUCCESS);

    pvNwk = ZPS_pvAplZdoGetNwkHandle();
    psNib = ZPS_psNwkNibGetHandle(pvNwk);
    u16Total = psNib->sTblSize.u16Rt;

    u8Length = DIAG_PAGE_PREFIX_LEN;

    for (i = u8Start; i < u16Total && u8Returned < u8Max; i++)
    {
        ZPS_tsNwkRtEntry sEntry = psNib->sTbl.psRt[i];
        uint8 u8RecordFlags = 0;

        if (sEntry.u16NwkDstAddr >= 0xFFFEU &&
            (u8Flags & DIAG_ROUTE_FLAG_INCLUDE_INACTIVE) == 0U)
        {
            continue;
        }

        u8RecordFlags = (uint8)(
              (sEntry.uAncAttrs.bfBitfields.u1ManyToOne       ? 0x01U : 0U)
            | (sEntry.uAncAttrs.bfBitfields.u1RouteRecordReqd ? 0x02U : 0U)
            | (sEntry.uAncAttrs.bfBitfields.u1RecentlyUsed    ? 0x04U : 0U)
            | (sEntry.uAncAttrs.bfBitfields.u1NoRouteCache    ? 0x08U : 0U)
            | (sEntry.uAncAttrs.bfBitfields.u1GroupIdFlag     ? 0x10U : 0U));

        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], (uint8)i,                u8Length );
        ZNC_BUF_U16_UPD ( &s_au8DiagTx[ u8Length ], sEntry.u16NwkDstAddr,    u8Length );
        ZNC_BUF_U16_UPD ( &s_au8DiagTx[ u8Length ], sEntry.u16NwkNxtHopAddr, u8Length );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], (uint8)sEntry.uAncAttrs.bfBitfields.u3Status, u8Length );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], sEntry.u8ExpiryCount,    u8Length );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8RecordFlags,           u8Length );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], (uint8)sEntry.uAncAttrs.bfBitfields.u8TxFailure, u8Length );

        u8Returned++;
    }

    {
        uint8 u8Prefix = 0;
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Prefix ], DIAG_RSP_VERSION,        u8Prefix );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Prefix ], E_SL_MSG_STATUS_SUCCESS, u8Prefix );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Prefix ], u8Flags,                 u8Prefix );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Prefix ], u8Start,                 u8Prefix );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Prefix ], (uint8)((u16Total > 0xFFU) ? 0xFFU : u16Total), u8Prefix );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Prefix ], u8Returned,              u8Prefix );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Prefix ], (uint8)((i > 0xFFU) ? 0xFFU : i), u8Prefix );
    }

    vSL_WriteMessage(E_SL_MSG_LOCAL_ROUTE_RSP, u8Length, s_au8DiagTx, 0);
}

/****************************************************************************/
/***        Local APS group operation (0x0D12 / 0x8D12)                  ***/
/****************************************************************************/

PUBLIC void CUSTOMDIAG_vHandleGroupOp(uint16 u16Len, const uint8 *pu8Rx)
{
    uint8        u8ReqVersion;
    uint8        u8Op;
    uint8        u8Endpoint;
    uint16       u16GroupId;
    uint8        u8Length = 0;
    ZPS_teStatus eZpsStatus = 0xFFU;
    uint8        u8Used = 0, u8Total = 0;
    uint8        u8OpStatus;
    bool         bEnabled = FALSE;
    ZPS_tsAplApsmeAIBGroupTable *psGroup;

    if (u16Len != DIAG_GROUP_OP_REQ_LEN)
    {
        vDiagSendStatus(E_SL_MSG_GROUP_OP_REQ, E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
        return;
    }
    u8ReqVersion = pu8Rx[0];
    u8Op         = pu8Rx[1];
    u8Endpoint   = pu8Rx[2];
    u16GroupId   = ZNC_RTN_U16(pu8Rx, 3);

    /* Validate: version, operation, endpoint (1..240), and group id where the
     * operation consumes it. Group id 0x0000 and reserved >0xFFF7 are invalid. */
    if (u8ReqVersion != DIAG_REQ_VERSION ||
        u8Op > DIAG_GROUP_OP_REMOVE_ALL ||
        u8Endpoint == 0U || u8Endpoint > 240U ||
        ((u8Op == DIAG_GROUP_OP_ADD || u8Op == DIAG_GROUP_OP_REMOVE) &&
         (u16GroupId == 0U || u16GroupId > 0xFFF7U)))
    {
        vDiagSendStatus(E_SL_MSG_GROUP_OP_REQ, E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
        return;
    }

    /* The endpoint must be an actually registered and enabled local endpoint. */
    if (ZPS_eAplAfGetEndpointState(u8Endpoint, &bEnabled) != ZPS_E_SUCCESS ||
        !bEnabled)
    {
        vDiagSendStatus(E_SL_MSG_GROUP_OP_REQ, E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
        return;
    }

    psGroup = ZPS_psAplAibGetAib()->psAplApsmeGroupTable;
    vDiagGroupUsage(psGroup, &u8Used, &u8Total);

    /* Capacity guard for add: refuse if the table is full and the group is not
     * already present. The stack API performs the actual persisted update via
     * the APS group table -- never a raw 0xF002 PDM write. */
    if (u8Op == DIAG_GROUP_OP_ADD && u8Used >= u8Total)
    {
        bool_t bExisting = FALSE;
        uint32 i;
        for (i = 0; psGroup != NULL && i < psGroup->u32SizeOfGroupTable; i++)
        {
            if (psGroup->psAplApsmeGroupTableId[i].u16Groupid == u16GroupId &&
                bDiagGroupEntryUsed(&psGroup->psAplApsmeGroupTableId[i]))
            {
                bExisting = TRUE;
                break;
            }
        }
        if (!bExisting)
        {
            vDiagSendStatus(E_SL_MSG_GROUP_OP_REQ, E_SL_MSG_STATUS_BUSY);
            return;
        }
    }

    switch (u8Op)
    {
        case DIAG_GROUP_OP_ADD:
            eZpsStatus = ZPS_eAplZdoGroupEndpointAdd(u16GroupId, u8Endpoint);
            break;
        case DIAG_GROUP_OP_REMOVE:
            eZpsStatus = ZPS_eAplZdoGroupEndpointRemove(u16GroupId, u8Endpoint);
            break;
        case DIAG_GROUP_OP_REMOVE_ALL:
            eZpsStatus = ZPS_eAplZdoGroupAllEndpointRemove(u8Endpoint);
            break;
        default:
            break;
    }

    /* The outer 0x8000 status reports only that the request was well-formed and
     * dispatched; the actual operation outcome is carried in the response's own
     * status byte (DIAG_GROUP_OP_STATUS_*) plus the raw zps_status field. */
    vDiagSendStatus(E_SL_MSG_GROUP_OP_REQ, E_SL_MSG_STATUS_SUCCESS);

    /* Refresh usage after the operation. */
    vDiagGroupUsage(psGroup, &u8Used, &u8Total);

    u8OpStatus = (eZpsStatus == ZPS_E_SUCCESS)
                 ? DIAG_GROUP_OP_STATUS_OK : DIAG_GROUP_OP_STATUS_ZPS_ERROR;

    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], DIAG_RSP_VERSION,        u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8OpStatus,              u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8Op,                    u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8Endpoint,              u8Length );
    ZNC_BUF_U16_UPD ( &s_au8DiagTx[ u8Length ], u16GroupId,              u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], (uint8)eZpsStatus,       u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8Used,                  u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8Total,                 u8Length );

    vSL_WriteMessage(E_SL_MSG_GROUP_OP_RSP, u8Length, s_au8DiagTx, 0);
}

/****************************************************************************/
/***        Local APS group paginated list (0x0D13 / 0x8D13)             ***/
/****************************************************************************/

PUBLIC void CUSTOMDIAG_vHandleGroupList(uint16 u16Len, const uint8 *pu8Rx)
{
    uint8        u8ReqVersion;
    uint8        u8Start;
    uint8        u8Max;
    uint8        u8Flags;
    uint8        u8Length = 0;
    uint8        u8Returned = 0;
    uint32       u32Total;
    uint32       i;
    ZPS_tsAplApsmeAIBGroupTable *psGroup;

    if (u16Len != DIAG_GROUP_LIST_REQ_LEN)
    {
        vDiagSendStatus(E_SL_MSG_GROUP_LIST_REQ, E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
        return;
    }
    u8ReqVersion = pu8Rx[0];
    u8Start      = pu8Rx[1];
    u8Max        = pu8Rx[2];
    u8Flags      = pu8Rx[3];

    if (u8ReqVersion != DIAG_REQ_VERSION ||
        u8Max == 0U || u8Max > DIAG_GROUP_LIST_MAX_RECORDS ||
        (u8Flags & ~DIAG_GROUP_LIST_FLAG_MASK) != 0U)
    {
        vDiagSendStatus(E_SL_MSG_GROUP_LIST_REQ, E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
        return;
    }

    vDiagSendStatus(E_SL_MSG_GROUP_LIST_REQ, E_SL_MSG_STATUS_SUCCESS);

    psGroup  = ZPS_psAplAibGetAib()->psAplApsmeGroupTable;
    u32Total = (psGroup != NULL) ? psGroup->u32SizeOfGroupTable : 0U;

    u8Length = DIAG_PAGE_PREFIX_LEN;

    for (i = u8Start; i < u32Total && u8Returned < u8Max; i++)
    {
        const ZPS_tsAplApsmeGroupTableEntry *psEntry =
            &psGroup->psAplApsmeGroupTableId[i];
        uint8 u8CountPos;
        uint8 u8EpCount = 0;
        uint16 u16Bit;

        if (!bDiagGroupEntryUsed(psEntry))
        {
            continue;
        }

        /* Physical table index first, so the host can correlate rows and resume
         * across holes even when a page returns fewer than max rows. */
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], (uint8)i,            u8Length );
        ZNC_BUF_U16_UPD ( &s_au8DiagTx[ u8Length ], psEntry->u16Groupid, u8Length );
        u8CountPos = u8Length;                 /* endpoint_count back-filled */
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], 0, u8Length );

        for (u16Bit = 0;
             u16Bit < (sizeof(psEntry->au8Endpoint) * 8U) &&
             u8EpCount < DIAG_GROUP_LIST_MAX_ENDPOINTS;
             u16Bit++)
        {
            if (psEntry->au8Endpoint[u16Bit >> 3] & (uint8)(1U << (u16Bit & 7U)))
            {
                uint8 u8Dummy = u8Length;
                ZNC_BUF_U8_UPD ( &s_au8DiagTx[ u8Length ], (uint8)u16Bit, u8Dummy );
                u8Length = u8Dummy;
                u8EpCount++;
            }
        }

        s_au8DiagTx[u8CountPos] = u8EpCount;   /* back-fill endpoint_count */
        u8Returned++;
    }

    {
        uint8 u8Prefix = 0;
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Prefix ], DIAG_RSP_VERSION,        u8Prefix );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Prefix ], E_SL_MSG_STATUS_SUCCESS, u8Prefix );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Prefix ], u8Flags,                 u8Prefix );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Prefix ], u8Start,                 u8Prefix );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Prefix ], (uint8)((u32Total > 0xFFU) ? 0xFFU : u32Total), u8Prefix );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Prefix ], u8Returned,              u8Prefix );
        ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Prefix ], (uint8)((i > 0xFFU) ? 0xFFU : i), u8Prefix );
    }

    vSL_WriteMessage(E_SL_MSG_GROUP_LIST_RSP, u8Length, s_au8DiagTx, 0);
}

/****************************************************************************/
/***        Coordinator manufacturer-code override (0x0D16 / 0x8D16)     ***/
/***                                                                       ***/
/*** Explicit, host-negotiated GET/SET/RESTORE of the coordinator's single ***/
/*** GLOBAL Node Descriptor manufacturer code, with mandatory readback.    ***/
/*** This replaces OpenLumi's hidden join-time mutation (removed in         ***/
/*** app_general_events_handler.c). There is exactly one Node Descriptor;   ***/
/*** the effective value returned is the global code -- the firmware makes  ***/
/*** no claim of per-device scoping (the host serialises access via its     ***/
/*** lease). No PDM / persistent write occurs: the descriptor lives in RAM  ***/
/*** and is advertised in ZDP Node Descriptor responses.                    ***/
/****************************************************************************/

PUBLIC void CUSTOMDIAG_vHandleManufCode(uint16 u16Len, const uint8 *pu8Rx)
{
    uint8   u8ReqVersion;
    uint8   u8Op;
    uint16  u16ReqCode;
    uint8   u8Length  = 0;
    uint8   u8Status;
    uint16  u16Effective;
    const uint16 u16Default = (uint16)ZCL_MANUFACTURER_CODE;
    ZPS_tsAplAfNodeDescriptor *psDesc;

    /* Strict fixed-length + version validation -> outer 0x8000 rejection. */
    if (u16Len != DIAG_MANUF_CODE_REQ_LEN)
    {
        vDiagSendStatus(E_SL_MSG_MANUFACTURER_CODE_REQ,
                        E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
        return;
    }
    u8ReqVersion = pu8Rx[0];
    u8Op         = pu8Rx[1];
    u16ReqCode   = ZNC_RTN_U16(pu8Rx, 2);

    if (u8ReqVersion != DIAG_REQ_VERSION)
    {
        vDiagSendStatus(E_SL_MSG_MANUFACTURER_CODE_REQ,
                        E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
        return;
    }

    /* Request is well-formed and dispatched: emit the stock success status,
     * then the versioned response whose own status byte carries the outcome. */
    vDiagSendStatus(E_SL_MSG_MANUFACTURER_CODE_REQ, E_SL_MSG_STATUS_SUCCESS);

    psDesc = ZPS_psGetLocalNodeDescriptor();

    if (psDesc == NULL)
    {
        /* No live descriptor: report INVALID with sentinel effective. */
        u8Status     = DIAG_MANUF_STATUS_INVALID;
        u16Effective = DIAG_U16_NA;
    }
    else
    {
        switch (u8Op)
        {
            case DIAG_MANUF_OP_GET:
                u16Effective = psDesc->u16ManufacturerCode;
                u8Status     = DIAG_MANUF_STATUS_OK;
                break;

            case DIAG_MANUF_OP_SET:
                psDesc->u16ManufacturerCode = u16ReqCode;
                /* Mandatory readback from the live descriptor. */
                u16Effective = psDesc->u16ManufacturerCode;
                u8Status     = (u16Effective == u16ReqCode)
                               ? DIAG_MANUF_STATUS_OK
                               : DIAG_MANUF_STATUS_INVALID;
                break;

            case DIAG_MANUF_OP_RESTORE:
                psDesc->u16ManufacturerCode = u16Default;
                u16Effective = psDesc->u16ManufacturerCode;
                u8Status     = (u16Effective == u16Default)
                               ? DIAG_MANUF_STATUS_OK
                               : DIAG_MANUF_STATUS_INVALID;
                break;

            default:
                /* Unknown op: do not mutate; report current effective. */
                u16Effective = psDesc->u16ManufacturerCode;
                u8Status     = DIAG_MANUF_STATUS_UNSUPPORTED_OP;
                break;
        }
    }

    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], DIAG_RSP_VERSION, u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8Status,         u8Length );
    ZNC_BUF_U16_UPD ( &s_au8DiagTx[ u8Length ], u16Effective,     u8Length );
    ZNC_BUF_U16_UPD ( &s_au8DiagTx[ u8Length ], u16Default,       u8Length );

    vSL_WriteMessage(E_SL_MSG_MANUFACTURER_CODE_RSP, u8Length, s_au8DiagTx, 0);
}

/****************************************************************************/
/***        END OF FILE                                                   ***/
/****************************************************************************/
