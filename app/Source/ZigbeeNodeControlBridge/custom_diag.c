/****************************************************************************
 *
 * MODULE:  custom_diag.c
 *
 * DESCRIPTION:
 *   Implementation of the compact versioned UART diagnostic and bounded
 *   local-control extension declared in custom_diag.h. See that header for
 *   the safety envelope. Nothing here mutates persistent network identity,
 *   exposes key material, or performs raw PDM / credential-flash access.
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
#include "zps_gen.h"
#include "mac_sap.h"
#include "bdb_api.h"
#include "rnd_pub.h"
#include "custom_diag.h"
/* APDU-pool usage helpers (u8GetApduUse). Minimal, dependency-free header;
 * implicit declaration of these is forbidden in this file, which is compiled
 * with -Werror=implicit-function-declaration. */
#include "zigate_apdu_diag.h"
#ifdef DIAG_HAVE_GP_COMMISSIONING
#include "app_green_power.h"
#endif

/****************************************************************************/
/***        External application state                                    ***/
/****************************************************************************/

extern tsBDB       sBDB;        /* BDB attributes, incl. bbdbNodeIsOnANetwork */
extern tsZllState  sZllState;   /* Application device/node state              */

/****************************************************************************/
/***        Local shared response buffer                                  ***/
/***                                                                       ***/
/*** Single static buffer reused by every handler. SerialLink streams LQI   ***/
/*** separately and never writes beyond the serialised payload.            ***/
/*** Safe because serial commands are dispatched sequentially from a single***/
/*** application-task context with no handler re-entrancy.                 ***/
/****************************************************************************/

PRIVATE uint8 s_au8DiagTx[DIAG_TX_BUFFER_SIZE];
PRIVATE uint16 s_u16BootPowerStatus;
PRIVATE uint8 s_u8BootResetFlags;
PRIVATE uint8 s_u8BootResetReason;
PRIVATE uint32 s_u32BootResetEpcr;
PRIVATE uint32 s_u32BootResetEear;
PRIVATE uint32 s_u32BootResetSp;
PRIVATE uint32 s_u32BootResetLr;

#define DIAG_RESET_RETAIN_MAGIC         (0x52535431UL)

typedef struct
{
    uint32 u32Magic;
    uint32 u32MagicInverse;
    uint8 u8Reason;
    uint8 u8ReasonInverse;
    uint16 u16Check;
    uint32 u32Epcr;
    uint32 u32Eear;
    uint32 u32Sp;
    uint32 u32Lr;
} tsDiagRetainedReset;

PRIVATE volatile tsDiagRetainedReset s_sRetainedReset
    __attribute__((section(".noinit")));

PRIVATE uint16 u16DiagResetContextCheck(
        uint8 u8Reason,
        uint32 u32Epcr,
        uint32 u32Eear,
        uint32 u32Sp,
        uint32 u32Lr)
{
    return (uint16)(0xA55AU ^
                    (uint16)DIAG_RESET_RETAIN_MAGIC ^
                    (uint16)(DIAG_RESET_RETAIN_MAGIC >> 16) ^
                    u8Reason ^
                    (uint16)u32Epcr ^ (uint16)(u32Epcr >> 16) ^
                    (uint16)u32Eear ^ (uint16)(u32Eear >> 16) ^
                    (uint16)u32Sp ^ (uint16)(u32Sp >> 16) ^
                    (uint16)u32Lr ^ (uint16)(u32Lr >> 16));
}

#ifdef OCB_TYPED_SUPPORT
/* One small bounded export snapshot prevents CORE from observing a mixture of
 * live states. No key bytes are ever copied into this object. */
typedef struct
{
    uint64 u64CoordinatorIeee;
    uint64 u64ExtPanId;
    uint64 u64TrustCenterIeee;
    uint32 u32TransactionId;
    uint32 u32SessionId;
    uint32 u32FieldBitmap;
    uint32 u32ChannelMask;
    uint32 u32NwkOutgoingCounter;
    uint32 u32Digest;
    uint16 u16PanId;
    uint8  u8Channel;
    uint8  u8UpdateId;
    uint8  u8SecurityLevel;
    uint8  u8NwkKeySequence;
    uint8  u8ApsFlags;
    uint8  u8ApsKeyType;
    uint8  u8Active;
} tsOcbExportSnapshot;

PRIVATE tsOcbExportSnapshot s_sOcbExport;
#endif

/****************************************************************************/
/***        Local helpers                                                 ***/
/****************************************************************************/

/* Emit the stock 8-byte E_SL_MSG_STATUS (0x8000) frame, matching the exact
 * field order used by every other command in app_Znc_cmds.c. */
PRIVATE void vDiagSendStatus(uint16 u16PacketType, uint8 u8Status)
{
    uint8 au8Status[8];
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

/****************************************************************************/
/***        Boot reset-cause snapshot (0x0D2B / 0x8D2B)                  ***/
/****************************************************************************/

PUBLIC void CUSTOMDIAG_vCaptureResetCause(void)
{
    uint16 u16ExpectedCheck;

    s_u16BootPowerStatus = u16AHI_PowerStatus();
    s_u8BootResetFlags = 0U;
    s_u8BootResetReason = DIAG_RESET_EXCEPTION_UNAVAILABLE;
    s_u32BootResetEpcr = DIAG_RESET_CONTEXT_UNAVAILABLE;
    s_u32BootResetEear = DIAG_RESET_CONTEXT_UNAVAILABLE;
    s_u32BootResetSp = DIAG_RESET_CONTEXT_UNAVAILABLE;
    s_u32BootResetLr = DIAG_RESET_CONTEXT_UNAVAILABLE;

    if (bAHI_WatchdogResetEvent())
    {
        s_u8BootResetFlags |= DIAG_RESET_FLAG_WATCHDOG;
    }
    if (bAHI_BrownOutEventResetStatus())
    {
        s_u8BootResetFlags |= DIAG_RESET_FLAG_BROWNOUT;
    }

    u16ExpectedCheck = u16DiagResetContextCheck(
            s_sRetainedReset.u8Reason,
            s_sRetainedReset.u32Epcr,
            s_sRetainedReset.u32Eear,
            s_sRetainedReset.u32Sp,
            s_sRetainedReset.u32Lr);
    if (s_sRetainedReset.u32Magic == DIAG_RESET_RETAIN_MAGIC &&
        s_sRetainedReset.u32MagicInverse == ~DIAG_RESET_RETAIN_MAGIC &&
        s_sRetainedReset.u8ReasonInverse ==
            (uint8)~s_sRetainedReset.u8Reason &&
        s_sRetainedReset.u16Check == u16ExpectedCheck)
    {
        s_u8BootResetReason = s_sRetainedReset.u8Reason;
        s_u32BootResetEpcr = s_sRetainedReset.u32Epcr;
        s_u32BootResetEear = s_sRetainedReset.u32Eear;
        s_u32BootResetSp = s_sRetainedReset.u32Sp;
        s_u32BootResetLr = s_sRetainedReset.u32Lr;
    }

    s_sRetainedReset.u32Magic = 0U;
    s_sRetainedReset.u32MagicInverse = 0U;
    s_sRetainedReset.u8Reason = DIAG_RESET_REASON_NONE;
    s_sRetainedReset.u8ReasonInverse = 0U;
    s_sRetainedReset.u16Check = 0U;
    s_sRetainedReset.u32Epcr = 0U;
    s_sRetainedReset.u32Eear = 0U;
    s_sRetainedReset.u32Sp = 0U;
    s_sRetainedReset.u32Lr = 0U;
}

PUBLIC void CUSTOMDIAG_vRetainResetReason(uint8 u8Reason)
{
    CUSTOMDIAG_vRetainExceptionContext(
            u8Reason,
            DIAG_RESET_CONTEXT_UNAVAILABLE,
            DIAG_RESET_CONTEXT_UNAVAILABLE,
            DIAG_RESET_CONTEXT_UNAVAILABLE,
            DIAG_RESET_CONTEXT_UNAVAILABLE);
}

PUBLIC void CUSTOMDIAG_vRetainExceptionContext(
        uint8 u8Reason,
        uint32 u32Epcr,
        uint32 u32Eear,
        uint32 u32Sp,
        uint32 u32Lr)
{
    s_sRetainedReset.u8Reason = u8Reason;
    s_sRetainedReset.u8ReasonInverse = (uint8)~u8Reason;
    s_sRetainedReset.u32Epcr = u32Epcr;
    s_sRetainedReset.u32Eear = u32Eear;
    s_sRetainedReset.u32Sp = u32Sp;
    s_sRetainedReset.u32Lr = u32Lr;
    s_sRetainedReset.u16Check = u16DiagResetContextCheck(
            u8Reason, u32Epcr, u32Eear, u32Sp, u32Lr);
    s_sRetainedReset.u32MagicInverse = ~DIAG_RESET_RETAIN_MAGIC;
    s_sRetainedReset.u32Magic = DIAG_RESET_RETAIN_MAGIC;
}

PUBLIC void CUSTOMDIAG_vHandleResetDiag(uint16 u16Len)
{
    uint8 u8Length = 0;

    if (u16Len != 0U)
    {
        vDiagSendStatus(E_SL_MSG_RESET_DIAG_REQ,
                        E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
        return;
    }

    vDiagSendStatus(E_SL_MSG_RESET_DIAG_REQ, E_SL_MSG_STATUS_SUCCESS);
    ZNC_BUF_U8_UPD  (&s_au8DiagTx[u8Length], DIAG_RSP_VERSION, u8Length);
    ZNC_BUF_U8_UPD  (&s_au8DiagTx[u8Length], E_SL_MSG_STATUS_SUCCESS, u8Length);
    ZNC_BUF_U8_UPD  (&s_au8DiagTx[u8Length], s_u8BootResetFlags, u8Length);
    ZNC_BUF_U16_UPD (&s_au8DiagTx[u8Length], s_u16BootPowerStatus, u8Length);
    ZNC_BUF_U8_UPD  (&s_au8DiagTx[u8Length], s_u8BootResetReason, u8Length);
    vSL_WriteMessage(E_SL_MSG_RESET_DIAG_RSP, u8Length, s_au8DiagTx, 0);
}

PUBLIC void CUSTOMDIAG_vHandleResetContext(uint16 u16Len)
{
    uint8 u8Length = 0;

    if (u16Len != 0U)
    {
        vDiagSendStatus(E_SL_MSG_RESET_CONTEXT_REQ,
                        E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
        return;
    }

    vDiagSendStatus(E_SL_MSG_RESET_CONTEXT_REQ, E_SL_MSG_STATUS_SUCCESS);
    ZNC_BUF_U8_UPD  (&s_au8DiagTx[u8Length], DIAG_RSP_VERSION, u8Length);
    ZNC_BUF_U8_UPD  (&s_au8DiagTx[u8Length], E_SL_MSG_STATUS_SUCCESS, u8Length);
    ZNC_BUF_U8_UPD  (&s_au8DiagTx[u8Length], s_u8BootResetReason, u8Length);
    ZNC_BUF_U8_UPD  (&s_au8DiagTx[u8Length], s_u8BootResetFlags, u8Length);
    ZNC_BUF_U16_UPD (&s_au8DiagTx[u8Length], s_u16BootPowerStatus, u8Length);
    ZNC_BUF_U32_UPD (&s_au8DiagTx[u8Length], s_u32BootResetEpcr, u8Length);
    ZNC_BUF_U32_UPD (&s_au8DiagTx[u8Length], s_u32BootResetEear, u8Length);
    ZNC_BUF_U32_UPD (&s_au8DiagTx[u8Length], s_u32BootResetSp, u8Length);
    ZNC_BUF_U32_UPD (&s_au8DiagTx[u8Length], s_u32BootResetLr, u8Length);
    vSL_WriteMessage(E_SL_MSG_RESET_CONTEXT_RSP, u8Length, s_au8DiagTx, 0);
}

/* rev4: canonical SIX-BIT TX-power code = GET & 0x3F.
 *
 * Per the HIL + MiniMac disassembly the MiniMac TX-power PIB is a 6-bit
 * two's-complement code and the raw i8 GET is sign-extended (e.g. code -8 reads
 * back as 0xFFFFFFF8). Masking the GET to 0x3F yields the canonical,
 * round-trippable six-bit code the host actually set. There is NO real 0x40
 * "3 dB tolerance" bit — that rev3 notion was wrong (0x40 is not
 * round-trippable), so it is not surfaced. Returns DIAG_U8_NA on GET failure. */
PRIVATE uint8 u8DiagTxPowerSixBit(void)
{
    uint32 u32TxPower = 0;

    if (eAppApiPlmeGet(PHY_PIB_ATTR_TX_POWER, &u32TxPower) == PHY_ENUM_SUCCESS)
    {
        return (uint8)(u32TxPower & PHY_PIB_TX_POWER_MASK);   /* 0x3F */
    }
    return DIAG_U8_NA;
}

/* Truthful signed interpretation of the six-bit two's-complement code:
 * (six & 0x20) ? six - 64 : six. This is the raw signed six-bit code, NOT an
 * exact radiated/effective dBm figure (the closed PHY exposes no exact
 * raw->dBm table). */
PRIVATE int8 i8DiagTxPowerSignedCode(uint8 u8SixBit)
{
    if (u8SixBit == DIAG_U8_NA) { return (int8)0x80; }  /* NA sentinel */
    if (u8SixBit & 0x20U)       { return (int8)((int)u8SixBit - 64); }
    return (int8)u8SixBit;
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

PRIVATE void vDiagGeneralDiagResponse(void);

PUBLIC void CUSTOMDIAG_vHandleGeneralDiag(uint16 u16Len)
{
    if (u16Len != 0)
    {
        vDiagSendStatus(E_SL_MSG_GENERAL_DIAG_REQ, E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
        return;
    }

    vDiagGeneralDiagResponse();
}

#ifdef OCB_TYPED_SUPPORT
    /****************************************************************************/
    /***        Typed OCB metadata export (0x0D18..0x0D1C)                    ***/
    /****************************************************************************/

    /* v2395/generated-config pins for the only internal layouts used here.  These
     * are typed live handles, never raw PDM layouts. */
    DIAG_STATIC_ASSERT(ZPS_MAX_CHANNEL_LIST_SIZE == 1U, ocb_one_channel_mask);
#ifndef ZPS_COORDINATOR
#error "OCB typed export is valid only for the generated coordinator configuration"
#endif
    DIAG_STATIC_ASSERT(sizeof(((ZPS_tsNwkSecMaterialSet *)0)->au8Key) == 16U,
                       ocb_v2395_nwk_key_width);

    PRIVATE uint32 u32OcbFnv1a(const uint8 *pu8Data, uint8 u8Length)
    {
        uint32 u32Hash = 2166136261UL;
        uint8 i;
        for (i = 0; i < u8Length; i++)
        {
            u32Hash ^= pu8Data[i];
            u32Hash *= 16777619UL;
        }
        return u32Hash;
    }

    PRIVATE bool_t bOcbCommonRequest(uint16 u16Len, const uint8 *pu8Rx,
                                     uint16 u16Expected, uint32 *pu32Transaction,
                                     uint32 *pu32Session)
    {
        if (u16Len != u16Expected)
        {
            return FALSE;
        }
        *pu32Transaction = ZNC_RTN_U32(pu8Rx, 2);
        *pu32Session = (u16Expected >= OCB_COMMON_REQ_LEN) ?
                       ZNC_RTN_U32(pu8Rx, 6) : 0U;
        return TRUE;
    }

    PRIVATE uint8 u8OcbRequestStatus(const uint8 *pu8Rx, uint32 u32Transaction,
                                     uint32 u32Session)
    {
        if (pu8Rx[0] != OCB_ABI_VERSION || pu8Rx[1] != OCB_SCHEMA_VERSION)
        {
            return OCB_STATUS_BAD_VERSION;
        }
        if (!s_sOcbExport.u8Active)
        {
            return OCB_STATUS_NO_SESSION;
        }
        if (s_sOcbExport.u32TransactionId != u32Transaction ||
            s_sOcbExport.u32SessionId != u32Session)
        {
            return OCB_STATUS_SESSION_MISMATCH;
        }
        return OCB_STATUS_OK;
    }

    PRIVATE uint8 u8OcbWritePrefix(uint32 u32Transaction, uint32 u32Session,
                                   uint8 u8Status)
    {
        uint8 u8Length = 0;
        ZNC_BUF_U8_UPD  (&s_au8DiagTx[u8Length], OCB_ABI_VERSION,    u8Length);
        ZNC_BUF_U8_UPD  (&s_au8DiagTx[u8Length], OCB_SCHEMA_VERSION, u8Length);
        ZNC_BUF_U32_UPD (&s_au8DiagTx[u8Length], u32Transaction,     u8Length);
        ZNC_BUF_U32_UPD (&s_au8DiagTx[u8Length], u32Session,         u8Length);
        ZNC_BUF_U8_UPD  (&s_au8DiagTx[u8Length], u8Status,           u8Length);
        return u8Length; /* 11 */
    }

    PRIVATE uint8 u8OcbSerialiseCore(uint8 u8Length)
    {
        ZNC_BUF_U32_UPD (&s_au8DiagTx[u8Length], s_sOcbExport.u32FieldBitmap,       u8Length);
        ZNC_BUF_U64_UPD (&s_au8DiagTx[u8Length], s_sOcbExport.u64CoordinatorIeee,   u8Length);
        ZNC_BUF_U16_UPD (&s_au8DiagTx[u8Length], s_sOcbExport.u16PanId,             u8Length);
        ZNC_BUF_U64_UPD (&s_au8DiagTx[u8Length], s_sOcbExport.u64ExtPanId,          u8Length);
        ZNC_BUF_U8_UPD  (&s_au8DiagTx[u8Length], s_sOcbExport.u8Channel,            u8Length);
        ZNC_BUF_U32_UPD (&s_au8DiagTx[u8Length], s_sOcbExport.u32ChannelMask,       u8Length);
        ZNC_BUF_U8_UPD  (&s_au8DiagTx[u8Length], s_sOcbExport.u8UpdateId,           u8Length);
        ZNC_BUF_U8_UPD  (&s_au8DiagTx[u8Length], s_sOcbExport.u8SecurityLevel,      u8Length);
        ZNC_BUF_U8_UPD  (&s_au8DiagTx[u8Length], s_sOcbExport.u8NwkKeySequence,     u8Length);
        ZNC_BUF_U32_UPD (&s_au8DiagTx[u8Length], s_sOcbExport.u32NwkOutgoingCounter,u8Length);
        ZNC_BUF_U64_UPD (&s_au8DiagTx[u8Length], s_sOcbExport.u64TrustCenterIeee,   u8Length);
        ZNC_BUF_U8_UPD  (&s_au8DiagTx[u8Length], s_sOcbExport.u8ApsFlags,           u8Length);
        ZNC_BUF_U8_UPD  (&s_au8DiagTx[u8Length], s_sOcbExport.u8ApsKeyType,         u8Length);
        return u8Length;
    }

    PUBLIC void CUSTOMDIAG_vHandleOcbExportBegin(uint16 u16Len, const uint8 *pu8Rx)
    {
        uint32 u32Transaction, u32Ignored;
        uint8 u8Length, u8Status = OCB_STATUS_OK;
        ZPS_tsNwkNib *psNib;
        ZPS_tsAplAib *psAib;
        uint8 u8MaskCount = 0;
        uint32 *pu32Masks;
        uint32 u32Channel = 0;

        if (!bOcbCommonRequest(u16Len, pu8Rx, OCB_BEGIN_REQ_LEN,
                               &u32Transaction, &u32Ignored))
        {
            vDiagSendStatus(E_SL_MSG_OCB_EXPORT_BEGIN_REQ,
                            E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
            return;
        }
        if (pu8Rx[0] != OCB_ABI_VERSION || pu8Rx[1] != OCB_SCHEMA_VERSION)
        {
            u8Status = OCB_STATUS_BAD_VERSION;
        }
        vDiagSendStatus(E_SL_MSG_OCB_EXPORT_BEGIN_REQ, E_SL_MSG_STATUS_SUCCESS);
        if (u8Status == OCB_STATUS_OK)
        {
            memset(&s_sOcbExport, 0, sizeof(s_sOcbExport));
            psNib = ZPS_psNwkNibGetHandle(ZPS_pvAplZdoGetNwkHandle());
            psAib = ZPS_psAplAibGetAib();
            pu32Masks = ZPS_pu32AplAibGetApsChannelMask(&u8MaskCount);

            s_sOcbExport.u32TransactionId = u32Transaction;
            s_sOcbExport.u32SessionId = RND_u32GetRand(1, 0xFFFFFFFFUL);
            if (s_sOcbExport.u32SessionId == 0U) { s_sOcbExport.u32SessionId = 1U; }
            s_sOcbExport.u64CoordinatorIeee = ZPS_u64AplZdoGetIeeeAddr();
            s_sOcbExport.u16PanId = psNib->sPersist.u16VsPanId;
            s_sOcbExport.u64ExtPanId = psNib->sPersist.u64ExtPanId;
            s_sOcbExport.u8UpdateId = psNib->sPersist.u8UpdateId;
            s_sOcbExport.u8SecurityLevel = psNib->u8SecurityLevel;
            s_sOcbExport.u8NwkKeySequence = psNib->sPersist.u8ActiveKeySeqNumber;
            s_sOcbExport.u32NwkOutgoingCounter = psNib->sTbl.u32OutFC;
            s_sOcbExport.u64TrustCenterIeee = psAib->u64ApsTrustCenterAddress;
            s_sOcbExport.u8ApsFlags =
                (psAib->bApsDesignatedCoordinator ? 1U : 0U) |
                (psAib->bApsUseInsecureJoin ? 2U : 0U) |
                (psAib->bDecryptInstallCode ? 4U : 0U);
            s_sOcbExport.u8ApsKeyType = psAib->u8KeyType;
            if (eAppApiPlmeGet(PHY_PIB_ATTR_CURRENT_CHANNEL, &u32Channel) ==
                PHY_ENUM_SUCCESS)
            {
                s_sOcbExport.u8Channel = (uint8)u32Channel;
                s_sOcbExport.u32FieldBitmap |= OCB_FIELD_CHANNEL;
            }
            if (pu32Masks != NULL && u8MaskCount == ZPS_MAX_CHANNEL_LIST_SIZE)
            {
                s_sOcbExport.u32ChannelMask = pu32Masks[0];
                s_sOcbExport.u32FieldBitmap |= OCB_FIELD_CHANNEL_MASK;
            }
            s_sOcbExport.u32FieldBitmap |=
                OCB_FIELD_COORD_IEEE | OCB_FIELD_PAN_ID | OCB_FIELD_EXT_PAN_ID |
                OCB_FIELD_NWK_UPDATE_ID | OCB_FIELD_SECURITY_LEVEL |
                OCB_FIELD_NWK_KEY_SEQUENCE | OCB_FIELD_NWK_OUT_COUNTER |
                OCB_FIELD_APS_TC_ADDRESS | OCB_FIELD_APS_STATE;
            s_sOcbExport.u8Active = 1U;

            /* Digest is FNV-1a over the exact 44-byte canonical CORE body,
             * beginning at field_bitmap and excluding the correlated prefix. */
            u8Length = u8OcbSerialiseCore(0U);
            s_sOcbExport.u32Digest = u32OcbFnv1a(s_au8DiagTx, u8Length);
        }

        u8Length = u8OcbWritePrefix(u32Transaction,
                                    (u8Status == OCB_STATUS_OK) ?
                                        s_sOcbExport.u32SessionId : 0U,
                                    u8Status);
        ZNC_BUF_U32_UPD(&s_au8DiagTx[u8Length], OCB_CAP_BITMAP, u8Length);
        ZNC_BUF_U32_UPD(&s_au8DiagTx[u8Length],
                        (u8Status == OCB_STATUS_OK) ?
                            s_sOcbExport.u32FieldBitmap : 0U, u8Length);
        vSL_WriteMessage(E_SL_MSG_OCB_EXPORT_BEGIN_RSP, u8Length, s_au8DiagTx, 0);
    }

    PUBLIC void CUSTOMDIAG_vHandleOcbExportCore(uint16 u16Len, const uint8 *pu8Rx)
    {
        uint32 u32Transaction, u32Session;
        uint8 u8Length, u8Status;
        if (!bOcbCommonRequest(u16Len, pu8Rx, OCB_COMMON_REQ_LEN,
                               &u32Transaction, &u32Session))
        {
            vDiagSendStatus(E_SL_MSG_OCB_EXPORT_CORE_REQ,
                            E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
            return;
        }
        u8Status = u8OcbRequestStatus(pu8Rx, u32Transaction, u32Session);
        vDiagSendStatus(E_SL_MSG_OCB_EXPORT_CORE_REQ, E_SL_MSG_STATUS_SUCCESS);
        u8Length = u8OcbWritePrefix(u32Transaction, u32Session, u8Status);
        if (u8Status == OCB_STATUS_OK)
        {
            u8Length = u8OcbSerialiseCore(u8Length);
        }
        else
        {
            memset(&s_au8DiagTx[u8Length], 0, OCB_CORE_RSP_LEN - u8Length);
            u8Length = OCB_CORE_RSP_LEN;
        }
        vSL_WriteMessage(E_SL_MSG_OCB_EXPORT_CORE_RSP, u8Length, s_au8DiagTx, 0);
    }

    PUBLIC void CUSTOMDIAG_vHandleOcbExportLinkKey(uint16 u16Len, const uint8 *pu8Rx)
    {
        uint32 u32Transaction, u32Session;
        uint64 u64RequestedEui = 0U;
        uint8 u8Length, u8Status;
        if (!bOcbCommonRequest(u16Len, pu8Rx, OCB_LINK_REQ_LEN,
                               &u32Transaction, &u32Session))
        {
            vDiagSendStatus(E_SL_MSG_OCB_EXPORT_LINK_KEY_REQ,
                            E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
            return;
        }
        u64RequestedEui = ZNC_RTN_U64(pu8Rx, 10);
        u8Status = u8OcbRequestStatus(pu8Rx, u32Transaction, u32Session);
        if (u8Status == OCB_STATUS_OK) { u8Status = OCB_STATUS_FIELD_UNAVAILABLE; }
        vDiagSendStatus(E_SL_MSG_OCB_EXPORT_LINK_KEY_REQ, E_SL_MSG_STATUS_SUCCESS);
        u8Length = u8OcbWritePrefix(u32Transaction, u32Session, u8Status);
        ZNC_BUF_U32_UPD(&s_au8DiagTx[u8Length], OCB_FIELD_LINK_KEYS, u8Length);
        ZNC_BUF_U64_UPD(&s_au8DiagTx[u8Length], u64RequestedEui, u8Length);
        ZNC_BUF_U8_UPD(&s_au8DiagTx[u8Length], 0U, u8Length); /* no key bytes */
        vSL_WriteMessage(E_SL_MSG_OCB_EXPORT_LINK_KEY_RSP, u8Length, s_au8DiagTx, 0);
    }

    PUBLIC void CUSTOMDIAG_vHandleOcbExportEnd(uint16 u16Len, const uint8 *pu8Rx)
    {
        uint32 u32Transaction, u32Session, u32Digest = 0U;
        uint8 u8Length, u8Status;
        if (!bOcbCommonRequest(u16Len, pu8Rx, OCB_COMMON_REQ_LEN,
                               &u32Transaction, &u32Session))
        {
            vDiagSendStatus(E_SL_MSG_OCB_EXPORT_END_REQ,
                            E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
            return;
        }
        u8Status = u8OcbRequestStatus(pu8Rx, u32Transaction, u32Session);
        if (u8Status == OCB_STATUS_OK) { u32Digest = s_sOcbExport.u32Digest; }
        vDiagSendStatus(E_SL_MSG_OCB_EXPORT_END_REQ, E_SL_MSG_STATUS_SUCCESS);
        u8Length = u8OcbWritePrefix(u32Transaction, u32Session, u8Status);
        ZNC_BUF_U8_UPD(&s_au8DiagTx[u8Length],
                       (u8Status == OCB_STATUS_OK) ? 1U : 0U, u8Length);
        ZNC_BUF_U32_UPD(&s_au8DiagTx[u8Length], u32Digest, u8Length);
        vSL_WriteMessage(E_SL_MSG_OCB_EXPORT_END_RSP, u8Length, s_au8DiagTx, 0);
        if (u8Status == OCB_STATUS_OK)
        {
            memset(&s_sOcbExport, 0, sizeof(s_sOcbExport));
        }
    }

    PUBLIC void CUSTOMDIAG_vHandleOcbStatus(uint16 u16Len, const uint8 *pu8Rx)
    {
        uint32 u32Transaction, u32Session;
        uint8 u8Length, u8Status;
        if (!bOcbCommonRequest(u16Len, pu8Rx, OCB_COMMON_REQ_LEN,
                               &u32Transaction, &u32Session))
        {
            vDiagSendStatus(E_SL_MSG_OCB_STATUS_REQ,
                            E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
            return;
        }
        u8Status = u8OcbRequestStatus(pu8Rx, u32Transaction, u32Session);
        vDiagSendStatus(E_SL_MSG_OCB_STATUS_REQ, E_SL_MSG_STATUS_SUCCESS);
        u8Length = u8OcbWritePrefix(u32Transaction, u32Session, u8Status);
        ZNC_BUF_U8_UPD(&s_au8DiagTx[u8Length], s_sOcbExport.u8Active, u8Length);
        ZNC_BUF_U32_UPD(&s_au8DiagTx[u8Length],
                        (u8Status == OCB_STATUS_OK) ?
                            s_sOcbExport.u32FieldBitmap : 0U, u8Length);
        ZNC_BUF_U32_UPD(&s_au8DiagTx[u8Length],
                        (u8Status == OCB_STATUS_OK) ?
                            s_sOcbExport.u32Digest : 0U, u8Length);
        vSL_WriteMessage(E_SL_MSG_OCB_STATUS_RSP, u8Length, s_au8DiagTx, 0);
    }
#endif /* OCB_TYPED_SUPPORT */

PRIVATE void vDiagGeneralDiagResponse(void)
{
    uint8         u8Length = 0;
    void         *pvNwk;
    ZPS_tsNwkNib *psNib;
    ZPS_tsAplApsmeAIBGroupTable *psGroup;
    uint32        u32Channel = 0;
    uint8         u8NbUsed = 0, u8NbTotal = 0;
    uint8         u8RtUsed = 0, u8RtTotal = 0;
    uint8         u8GrUsed = 0, u8GrTotal = 0;
    uint8         u8TxSixBit;
    uint8         u8DiagFlags = 0;
    uint8         u8TclkCb, u8TclkAddRepl, u8TclkCred;

    vDiagSendStatus(E_SL_MSG_GENERAL_DIAG_REQ, E_SL_MSG_STATUS_SUCCESS);

    pvNwk = ZPS_pvAplZdoGetNwkHandle();
    psNib = ZPS_psNwkNibGetHandle(pvNwk);
    psGroup = ZPS_psAplAibGetAib()->psAplApsmeGroupTable;

    (void)eAppApiPlmeGet(PHY_PIB_ATTR_CURRENT_CHANNEL, &u32Channel);
    vDiagNeighbourUsage(psNib, &u8NbUsed, &u8NbTotal);
    vDiagRouteUsage(psNib, &u8RtUsed, &u8RtTotal);
    vDiagGroupUsage(psGroup, &u8GrUsed, &u8GrTotal);
    u8TxSixBit = u8DiagTxPowerSixBit();

    /* TCLK summary: the TCLK diagnostic subsystem (crypto-path --wrap
     * interposition + full internal security-state export) has been REMOVED as
     * security-sensitive. These three fields are retained in the 0x0D1F wire
     * layout for backward compatibility but are now always NA, and the
     * TCLK_UNAVAILABLE flag is always asserted so the host can distinguish
     * "removed" from a transient snapshot miss. */
    u8DiagFlags  |= DIAG_GENDIAG_FLAG_TCLK_UNAVAILABLE;
    u8TclkCb      = DIAG_U8_NA;
    u8TclkAddRepl = DIAG_U8_NA;
    u8TclkCred    = DIAG_U8_NA;

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

    /* rev6 TX power: canonical six-bit code (GET & 0x3F), repeated canonical
     * level, signed six-bit code. Protocol 1.2 defines both unsigned fields as
     * the native round-trippable code; the signed code is NOT an exact
     * radiated dBm value.
     *
     * DELIBERATE DIVERGENCE from the legacy 0x8806/0x8807 frames: those keep
     * byte0 = six-bit code but byte1 = the LEGACY MAPPED LEVEL produced by the
     * threshold ladder in app_Znc_cmds.c (<=31 -> 0, <=39 -> 32, <=51 -> 20,
     * else 9). Here byte1 is the six-bit code again. The two representations
     * therefore DO NOT agree, and that is intended: 0x8D1F is the canonical
     * raw-register view, 0x8806/0x8807 preserve the legacy ZiGate mapping that
     * existing hosts parse. Do not "harmonise" them - changing 0x8806/0x8807
     * would break deployed hosts, and changing 0x8D1F would reintroduce a
     * lossy mapping into the diagnostic path. Hosts must key the 0x8D1F
     * interpretation off build revision >= 6 (see DIAG_FW_BUILD_ID). */
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8TxSixBit,  u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8TxSixBit,  u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], (uint8)i8DiagTxPowerSignedCode(u8TxSixBit), u8Length );

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
         * lookup index is within the MAC address-table bounds; otherwise the
         * mapped IEEE is meaningless, so emit the all-FF unavailable sentinel.
         * u16Lookup indexes the MAC address table (pu64AddrExtAddrMap, sized
         * u16MacAddTableSize) consumed by ZPS_u64NwkNibGetMappedIeeeAddr() --
         * NOT the smaller nwkAddressMap (u16AddrMap). The removed/unused
         * sentinel 0xFFFF is naturally excluded by this bound. */
        if (u8Used != 0U && sEntry.u16Lookup < psNib->sTblSize.u16MacAddTableSize)
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
    /* The true shipped default is the manufacturer code the generated Node
     * Descriptor booted with. Snapshot it exactly ONCE, before any SET can
     * mutate the single global descriptor, and treat that runtime value as the
     * source of truth for RESTORE_DEFAULT and the "default code" field. Because
     * the only former mutator (OpenLumi's join-time Xiaomi rewrite) has been
     * removed, the first handler entry always observes the boot default. */
    static bool_t s_bManufDefaultCaptured  = FALSE;
    static uint16 s_u16ManufDefaultSnapshot = DIAG_MANUF_CODE_SHIPPED_DEFAULT;

    uint8   u8ReqVersion;
    uint8   u8Op;
    uint16  u16ReqCode;
    uint8   u8Length  = 0;
    uint8   u8Status;
    uint16  u16Effective;
    uint16  u16Default = DIAG_MANUF_CODE_SHIPPED_DEFAULT;
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
        /* No live descriptor: report INVALID with sentinel effective and the
         * best-known default (prior snapshot or the shipped fallback). */
        u8Status     = DIAG_MANUF_STATUS_INVALID;
        u16Effective = DIAG_U16_NA;
        u16Default   = s_u16ManufDefaultSnapshot;
    }
    else
    {
        /* Capture the shipped default exactly once, BEFORE applying any op. */
        if (!s_bManufDefaultCaptured)
        {
            s_u16ManufDefaultSnapshot = psDesc->u16ManufacturerCode;
            s_bManufDefaultCaptured   = TRUE;
        }
        u16Default = s_u16ManufDefaultSnapshot;

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
/***        Green Power proxy commissioning window (0x0D17 / 0x8D17)      ***/
/***                                                                       ***/
/*** Negotiated, explicit, bounded control of the LOCAL Green Power proxy  ***/
/*** commissioning state machine. The stock firmware exposes no GP command,***/
/*** and a host-built ZGP Proxy Commissioning Mode frame pushed through the***/
/*** ordinary 0x0530 data request is not equivalent: it is encoded as an   ***/
/*** acknowledged unicast to 0xFFFC (rejected on the rev6 HIL with         ***/
/*** ZPS_APL_APS_E_NO_ACK / 0xA6) and, even when it does leave the node, it***/
/*** never opens or bounds the coordinator's OWN proxy commissioning       ***/
/*** window. This command drives the SDK state machine on the locally      ***/
/*** mapped GP endpoint (GREENPOWER_END_POINT_ID) towards APS endpoint 242 ***/
/*** and lets the SDK's 20 ms GP scheduler close the window on expiry.     ***/
/***                                                                       ***/
/*** Read-only-or-local envelope is preserved: no key material is accepted ***/
/*** or emitted, nothing is written to PDM, and GP shared-key programming  ***/
/*** remains unimplemented and unadvertised.                               ***/
/****************************************************************************/

#ifdef DIAG_HAVE_GP_COMMISSIONING
PUBLIC void CUSTOMDIAG_vHandleGPCommission(uint16 u16Len, const uint8 *pu8Rx)
{
    uint8  u8ReqVersion;
    uint32 u32TransactionId;
    uint8  u8Action;
    uint8  u8Timeout;
    uint8  u8Length    = 0;
    uint8  u8Status;
    uint8  u8Mode;
    uint8  u8Effective = 0;
    uint8  u8GpStatus;

    /* Strict fixed-length validation -> outer 0x8000 rejection, no 0x8D17. */
    if (u16Len != DIAG_GP_COMMISSION_REQ_LEN)
    {
        vDiagSendStatus(E_SL_MSG_GP_COMMISSION_REQ,
                        E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
        return;
    }

    u8ReqVersion     = pu8Rx[0];
    /* Big-endian u32, the same on-wire convention as the 0x0D0F nonce. A
     * 32-bit id cannot wrap within the lifetime of a queued host request, so
     * a late response can never be correlated to a later transaction. */
    u32TransactionId = ZNC_RTN_U32(pu8Rx, 1);
    u8Action         = pu8Rx[5];
    u8Timeout        = pu8Rx[6];

    if (u8ReqVersion != DIAG_REQ_VERSION)
    {
        vDiagSendStatus(E_SL_MSG_GP_COMMISSION_REQ,
                        E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
        return;
    }

    /* The transaction id is opaque: every 32-bit value is legal and is never
     * interpreted here, only echoed. Rejecting values would make the host's
     * free-running counter a shared protocol concern for no benefit. */

    /* Strict action/timeout pairing: DISABLE requires timeout 0, ENABLE
     * requires 1..255. Anything else is a host bug, not a runtime failure. */
    if (u8Action == DIAG_GP_ACTION_DISABLE)
    {
        if (u8Timeout != 0U)
        {
            vDiagSendStatus(E_SL_MSG_GP_COMMISSION_REQ,
                            E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
            return;
        }
    }
    else if (u8Action == DIAG_GP_ACTION_ENABLE)
    {
        if ((u8Timeout < DIAG_GP_TIMEOUT_MIN) || (u8Timeout > DIAG_GP_TIMEOUT_MAX))
        {
            vDiagSendStatus(E_SL_MSG_GP_COMMISSION_REQ,
                            E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
            return;
        }
    }
    else
    {
        vDiagSendStatus(E_SL_MSG_GP_COMMISSION_REQ,
                        E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
        return;
    }

    /* Request is well-formed and dispatched: emit the stock success status,
     * then the versioned response whose own status byte carries the outcome. */
    vDiagSendStatus(E_SL_MSG_GP_COMMISSION_REQ, E_SL_MSG_STATUS_SUCCESS);

    u8GpStatus = u8App_GP_SetProxyCommissioningMode(
                     (bool_t)(u8Action == DIAG_GP_ACTION_ENABLE),
                     u8Timeout,
                     &u8Effective);

    if (u8GpStatus == 0U)   /* E_ZCL_SUCCESS */
    {
        u8Status = DIAG_GP_COMMISSION_STATUS_OK;
        u8Mode   = (u8Action == DIAG_GP_ACTION_ENABLE)
                   ? DIAG_GP_MODE_COMMISSIONING
                   : DIAG_GP_MODE_OPERATING;
    }
    else
    {
        /* The wrapper rolls the local state back on a failed enter and always
         * closes the window on a failed exit request, so the honest effective
         * mode after any failure is "operating". */
        u8Status    = DIAG_GP_COMMISSION_STATUS_GP_ERROR;
        u8Mode      = DIAG_GP_MODE_OPERATING;
        u8Effective = 0;
    }

    /* The transaction id is echoed on EVERY response path reached from a
     * structurally valid request, so the host can correlate a Green Power
     * failure exactly as it correlates a success.
     *
     * status and gp_status are consistent by construction: OK is emitted only
     * when the GP call returned E_ZCL_SUCCESS (0), and GP_ERROR only when it
     * did not. The host enforces the same invariant on receive. */
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], DIAG_RSP_VERSION, u8Length );
    ZNC_BUF_U32_UPD ( &s_au8DiagTx[ u8Length ], u32TransactionId, u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8Status,         u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8Mode,           u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8Effective,      u8Length );
    ZNC_BUF_U8_UPD  ( &s_au8DiagTx[ u8Length ], u8GpStatus,       u8Length );

    vSL_WriteMessage(E_SL_MSG_GP_COMMISSION_RSP, u8Length, s_au8DiagTx, 0);
}
#endif /* DIAG_HAVE_GP_COMMISSIONING */

/****************************************************************************/
/***        END OF FILE                                                   ***/
/****************************************************************************/
