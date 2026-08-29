/****************************************************************************
 *
 * EXPERIMENTAL trusted-local-UART OCB key export.
 *
 * There is deliberately no embedded secret and no claim of authentication or
 * confidentiality. The short nonce confirmation only prevents accidental
 * invocation by software speaking the wrong protocol.
 *
 ****************************************************************************/

#include <jendefs.h>
#include <string.h>
#include "SerialLink.h"
#include "app_Znc_cmds.h"
#include "app_common.h"
#include "AppHardwareApi.h"
#include "AppApi.h"
#include "AHI_AES.h"
#include "PDM.h"
#include "pdum_apl.h"
#include "zps_gen.h"
#include "zps_apl.h"
#include "zps_apl_zdo.h"
#include "zps_apl_aib.h"
#include "zps_apl_af.h"
#include "zps_nwk_nib.h"
#include "zps_nwk_sec.h"
#include "rnd_pub.h"
#include "ocb_experimental.h"
#include "zigate_apdu_diag.h"

#ifndef OCB_KEY_EXPORT_RESTORE_EXPERIMENTAL
#error "ocb_experimental.c must only be compiled in the experimental build"
#endif

#define OCBEXP_TX_MAX                     (96U)
#define OCBEXP_TX_LQI_RESERVE             (1U)
#define OCBEXP_EXPECTED_SEC_MATERIALS     (2U)
#define OCBEXP_EXPECTED_APS_KEY_ENTRIES   (1U)
#define OCBEXP_EXPECTED_MAC_TABLE         (36U)
#define OCBEXP_FLASH_TCLK_ENTRIES         (70U)

#define OCBEXP_STATIC_ASSERT(cond, tag) \
    typedef char ocbexp_static_assert_##tag[(cond) ? 1 : -1]

OCBEXP_STATIC_ASSERT(sizeof(AESSW_Block_u) == 16U, aes_key_width);
OCBEXP_STATIC_ASSERT(sizeof(((ZPS_tsNwkSecMaterialSet *)0)->au8Key) == 16U,
                     nwk_key_width);
OCBEXP_STATIC_ASSERT(sizeof(((ZPS_tsAplApsKeyDescriptorEntry *)0)->au8LinkKey) == 16U,
                     aps_key_width);
OCBEXP_STATIC_ASSERT(sizeof(ZPS_tsAplApsKeyDescriptorEntry) == 24U,
                     v2395_legacy_key_descriptor_layout);
OCBEXP_STATIC_ASSERT(OCBEXP_SECRET_CORE_RSP_LEN <= OCBEXP_TX_MAX, core_fits);
OCBEXP_STATIC_ASSERT(OCBEXP_LINK_RSP_LEN <= OCBEXP_TX_MAX, link_fits);

/* This symbol is exported by v2395 libZPSAPL_LEGACY_JN516x.a but omitted from
 * the public header. The application already uses this exact declaration in
 * app_Znc_cmds.c. It is isolated here behind the experimental flag. */
extern PUBLIC bool_t zps_bGetFlashCredential(uint64 u64IeeeAddr,
                                              AESSW_Block_u *puKey,
                                              uint16 *pu16Index,
                                              bool_t bTcCred,
                                              bool_t bUpdate);

PRIVATE uint8 s_au8ExpTx[OCBEXP_TX_MAX + OCBEXP_TX_LQI_RESERVE];
PRIVATE uint32 s_u32Challenge;
PRIVATE uint32 s_u32UnlockTransaction;
PRIVATE uint32 s_u32UnlockStarted;
PRIVATE bool_t s_bUnlocked;

PRIVATE void vExpWipe(void *pvData, uint16 u16Length)
{
    volatile uint8 *pu8 = (volatile uint8 *)pvData;
    while (u16Length-- != 0U)
    {
        *pu8++ = 0U;
    }
}

PRIVATE void vExpSendOuterStatus(uint16 u16Type, uint8 u8Status)
{
    uint8 au8Status[9];
    uint8 u8Length = 0;
    ZNC_BUF_U8_UPD  (&au8Status[u8Length], u8Status, u8Length);
    ZNC_BUF_U8_UPD  (&au8Status[u8Length], 0U, u8Length);
    ZNC_BUF_U16_UPD (&au8Status[u8Length], u16Type, u8Length);
    ZNC_BUF_U8_UPD  (&au8Status[u8Length], 0U, u8Length);
    ZNC_BUF_U8_UPD  (&au8Status[u8Length], 0U, u8Length);
    ZNC_BUF_U8_UPD  (&au8Status[u8Length], PDUM_u8GetNpduUse(), u8Length);
    ZNC_BUF_U8_UPD  (&au8Status[u8Length], u8GetApduUse(), u8Length);
    vSL_WriteMessage(E_SL_MSG_STATUS, u8Length, au8Status, 0);
}

PRIVATE bool_t bExpParse(uint16 u16Type, uint16 u16Len, uint16 u16Expected,
                         const uint8 *pu8Rx, uint32 *pu32Transaction)
{
    if (u16Len != u16Expected)
    {
        vExpSendOuterStatus(u16Type, E_SL_MSG_STATUS_INCORRECT_PARAMETERS);
        return FALSE;
    }
    *pu32Transaction = ZNC_RTN_U32(pu8Rx, 2);
    vExpSendOuterStatus(u16Type, E_SL_MSG_STATUS_SUCCESS);
    return TRUE;
}

PRIVATE uint8 u8ExpPrefix(uint32 u32Transaction, uint8 u8Status)
{
    uint8 u8Length = 0;
    ZNC_BUF_U8_UPD  (&s_au8ExpTx[u8Length], OCBEXP_ABI_VERSION, u8Length);
    ZNC_BUF_U8_UPD  (&s_au8ExpTx[u8Length], OCBEXP_SCHEMA_VERSION, u8Length);
    ZNC_BUF_U32_UPD (&s_au8ExpTx[u8Length], u32Transaction, u8Length);
    ZNC_BUF_U8_UPD  (&s_au8ExpTx[u8Length], u8Status, u8Length);
    return u8Length; /* 7 */
}

PRIVATE bool_t bExpVersion(const uint8 *pu8Rx)
{
    return pu8Rx[0] == OCBEXP_ABI_VERSION &&
           pu8Rx[1] == OCBEXP_SCHEMA_VERSION;
}

PRIVATE void vExpLock(void)
{
    s_bUnlocked = FALSE;
    s_u32Challenge = 0U;
    s_u32UnlockTransaction = 0U;
    s_u32UnlockStarted = 0U;
}

PRIVATE bool_t bExpUnlocked(uint32 u32Transaction)
{
    uint32 u32Elapsed;
    if (!s_bUnlocked || s_u32UnlockTransaction != u32Transaction)
    {
        return FALSE;
    }
    u32Elapsed = u32AHI_TickTimerRead() - s_u32UnlockStarted;
    if (u32Elapsed > OCBEXP_UNLOCK_TICKS)
    {
        vExpLock();
        return FALSE;
    }
    return TRUE;
}

PRIVATE uint8 u8ExpRemainingSeconds(uint32 u32Transaction)
{
    uint32 u32Elapsed;
    uint32 u32Remaining;
    if (!bExpUnlocked(u32Transaction))
    {
        return 0U;
    }
    u32Elapsed = u32AHI_TickTimerRead() - s_u32UnlockStarted;
    u32Remaining = OCBEXP_UNLOCK_TICKS - u32Elapsed;
    return (uint8)((u32Remaining + OCBEXP_TICK_HZ - 1U) / OCBEXP_TICK_HZ);
}

PRIVATE bool_t bExpLayoutValid(ZPS_tsNwkNib *psNib, ZPS_tsAplAib *psAib)
{
    return psNib != NULL && psAib != NULL &&
           psNib->sTbl.psSecMatSet != NULL &&
           psNib->sTblSize.u8SecMatSet == OCBEXP_EXPECTED_SEC_MATERIALS &&
           psNib->sTblSize.u16MacAddTableSize == OCBEXP_EXPECTED_MAC_TABLE &&
           psAib->psAplDeviceKeyPairTable != NULL &&
           psAib->psAplDeviceKeyPairTable->psAplApsKeyDescriptorEntry != NULL &&
           psAib->psAplDeviceKeyPairTable->u16SizeOfKeyDescriptorTable ==
               OCBEXP_EXPECTED_APS_KEY_ENTRIES &&
           psAib->psAplDefaultTCAPSLinkKey != NULL &&
           psAib->pu32IncomingFrameCounter != NULL;
}

PUBLIC void OCBEXP_vHandleChallenge(uint16 u16Len, const uint8 *pu8Rx)
{
    uint32 u32Transaction;
    uint8 u8Length;
    uint8 u8Status = OCBEXP_STATUS_OK;
    if (!bExpParse(E_SL_MSG_OCBEXP_CHALLENGE_REQ, u16Len, OCBEXP_REQ_LEN,
                   pu8Rx, &u32Transaction))
    {
        return;
    }
    if (!bExpVersion(pu8Rx))
    {
        u8Status = OCBEXP_STATUS_BAD_VERSION;
    }
    vExpLock();
    if (u8Status == OCBEXP_STATUS_OK)
    {
        s_u32Challenge = RND_u32GetRand(1U, 0xFFFFFFFFUL);
        s_u32UnlockTransaction = u32Transaction;
        s_u32UnlockStarted = u32AHI_TickTimerRead();
    }
    u8Length = u8ExpPrefix(u32Transaction, u8Status);
    ZNC_BUF_U32_UPD(&s_au8ExpTx[u8Length], s_u32Challenge, u8Length);
    ZNC_BUF_U8_UPD(&s_au8ExpTx[u8Length], OCBEXP_UNLOCK_SECONDS, u8Length);
    ZNC_BUF_U32_UPD(&s_au8ExpTx[u8Length], OCBEXP_LIMIT_BITMAP, u8Length);
    vSL_WriteMessage(E_SL_MSG_OCBEXP_CHALLENGE_RSP, u8Length, s_au8ExpTx, 0);
    vExpWipe(s_au8ExpTx, sizeof(s_au8ExpTx));
}

PUBLIC void OCBEXP_vHandleUnlock(uint16 u16Len, const uint8 *pu8Rx)
{
    uint32 u32Transaction;
    uint32 u32Nonce;
    uint32 u32Confirmation;
    uint32 u32Elapsed;
    uint8 u8Length;
    uint8 u8Status = OCBEXP_STATUS_LOCKED;
    if (!bExpParse(E_SL_MSG_OCBEXP_UNLOCK_REQ, u16Len, OCBEXP_UNLOCK_REQ_LEN,
                   pu8Rx, &u32Transaction))
    {
        return;
    }
    if (!bExpVersion(pu8Rx))
    {
        u8Status = OCBEXP_STATUS_BAD_VERSION;
    }
    else
    {
        u32Nonce = ZNC_RTN_U32(pu8Rx, 6);
        u32Confirmation = ZNC_RTN_U32(pu8Rx, 10);
        u32Elapsed = u32AHI_TickTimerRead() - s_u32UnlockStarted;
        if (u32Transaction == s_u32UnlockTransaction &&
            u32Nonce == s_u32Challenge &&
            u32Elapsed <= OCBEXP_UNLOCK_TICKS &&
            u32Confirmation ==
                (u32Nonce ^ u32Transaction ^ OCBEXP_CONFIRM_MAGIC))
        {
            s_bUnlocked = TRUE;
            s_u32UnlockStarted = u32AHI_TickTimerRead();
            s_u32Challenge = 0U;
            u8Status = OCBEXP_STATUS_OK;
        }
    }
    if (u8Status != OCBEXP_STATUS_OK) { vExpLock(); }
    u8Length = u8ExpPrefix(u32Transaction, u8Status);
    ZNC_BUF_U8_UPD(&s_au8ExpTx[u8Length],
                   (u8Status == OCBEXP_STATUS_OK) ? OCBEXP_UNLOCK_SECONDS : 0U,
                   u8Length);
    ZNC_BUF_U32_UPD(&s_au8ExpTx[u8Length], OCBEXP_LIMIT_BITMAP, u8Length);
    vSL_WriteMessage(E_SL_MSG_OCBEXP_UNLOCK_RSP, u8Length, s_au8ExpTx, 0);
    vExpWipe(s_au8ExpTx, sizeof(s_au8ExpTx));
}

PUBLIC void OCBEXP_vHandleSecretCore(uint16 u16Len, const uint8 *pu8Rx)
{
    uint32 u32Transaction;
    uint32 u32Available = 0U;
    uint8 u8Length;
    uint8 u8Status = OCBEXP_STATUS_OK;
    uint8 u8NwkSeq = 0U;
    uint8 u8TcType = 0U;
    uint8 au8NwkKey[16];
    uint8 au8TcKey[16];
    uint32 u32NwkOut = 0U, u32TcOut = 0U, u32TcIn = 0U;
    uint8 i;
    ZPS_tsNwkNib *psNib;
    ZPS_tsAplAib *psAib;

    memset(au8NwkKey, 0, sizeof(au8NwkKey));
    memset(au8TcKey, 0, sizeof(au8TcKey));
    if (!bExpParse(E_SL_MSG_OCBEXP_SECRET_CORE_REQ, u16Len, OCBEXP_REQ_LEN,
                   pu8Rx, &u32Transaction))
    {
        return;
    }
    if (!bExpVersion(pu8Rx)) { u8Status = OCBEXP_STATUS_BAD_VERSION; }
    else if (!bExpUnlocked(u32Transaction)) { u8Status = OCBEXP_STATUS_LOCKED; }

    psNib = ZPS_psNwkNibGetHandle(ZPS_pvAplZdoGetNwkHandle());
    psAib = ZPS_psAplAibGetAib();
    if (u8Status == OCBEXP_STATUS_OK && !bExpLayoutValid(psNib, psAib))
    {
        u8Status = OCBEXP_STATUS_LAYOUT_MISMATCH;
    }
    if (u8Status == OCBEXP_STATUS_OK)
    {
        u8NwkSeq = psNib->sPersist.u8ActiveKeySeqNumber;
        if (ZPS_bNwkSecHaveNetworkKey(ZPS_pvAplZdoGetNwkHandle()))
        {
            for (i = 0; i < psNib->sTblSize.u8SecMatSet; i++)
            {
                if (psNib->sTbl.psSecMatSet[i].u8KeySeqNum == u8NwkSeq)
                {
                    memcpy(au8NwkKey, psNib->sTbl.psSecMatSet[i].au8Key, 16U);
                    u32Available |= OCBEXP_AVAIL_NWK_KEY;
                    break;
                }
            }
        }
        u32NwkOut = psNib->sTbl.u32OutFC;
        u32Available |= OCBEXP_AVAIL_NWK_OUT_COUNTER;

        memcpy(au8TcKey, psAib->psAplDefaultTCAPSLinkKey->au8LinkKey, 16U);
        u8TcType = psAib->u8KeyType;
        u32TcOut = psAib->psAplDefaultTCAPSLinkKey->u32OutgoingFrameCounter;
        /* Generated v2395 legacy layout pins default TC key at storage[1]. */
        u32TcIn = psAib->pu32IncomingFrameCounter[1];
        u32Available |= OCBEXP_AVAIL_TC_LINK_KEY |
                        OCBEXP_AVAIL_APS_OUT_COUNTER |
                        OCBEXP_AVAIL_APS_IN_COUNTER;
    }

    u8Length = u8ExpPrefix(u32Transaction, u8Status);
    ZNC_BUF_U32_UPD(&s_au8ExpTx[u8Length], u32Available, u8Length);
    ZNC_BUF_U32_UPD(&s_au8ExpTx[u8Length], OCBEXP_LIMIT_BITMAP, u8Length);
    ZNC_BUF_U8_UPD(&s_au8ExpTx[u8Length], u8NwkSeq, u8Length);
    memcpy(&s_au8ExpTx[u8Length], au8NwkKey, 16U); u8Length += 16U;
    ZNC_BUF_U32_UPD(&s_au8ExpTx[u8Length], u32NwkOut, u8Length);
    ZNC_BUF_U8_UPD(&s_au8ExpTx[u8Length], u8TcType, u8Length);
    memcpy(&s_au8ExpTx[u8Length], au8TcKey, 16U); u8Length += 16U;
    ZNC_BUF_U32_UPD(&s_au8ExpTx[u8Length], u32TcOut, u8Length);
    ZNC_BUF_U32_UPD(&s_au8ExpTx[u8Length], u32TcIn, u8Length);
    vSL_WriteMessage(E_SL_MSG_OCBEXP_SECRET_CORE_RSP, u8Length, s_au8ExpTx, 0);
    vExpWipe(au8NwkKey, sizeof(au8NwkKey));
    vExpWipe(au8TcKey, sizeof(au8TcKey));
    vExpWipe(s_au8ExpTx, sizeof(s_au8ExpTx));
}

PUBLIC void OCBEXP_vHandleLinkKey(uint16 u16Len, const uint8 *pu8Rx)
{
    uint32 u32Transaction;
    uint32 u32Available = 0U;
    uint32 u32Outgoing = 0U, u32Incoming = 0U;
    uint64 u64Eui = 0U;
    uint16 u16CredIndex = 0xFFFFU;
    uint8 u8Kind = 0U, u8Index = 0U, u8KeyType = 0U;
    uint8 u8Status = OCBEXP_STATUS_OK, u8Length;
    uint8 au8Key[16];
    AESSW_Block_u uFlashKey;
    ZPS_tsNwkNib *psNib;
    ZPS_tsAplAib *psAib;
    ZPS_tsAplApsKeyDescriptorEntry *psKey = NULL;

    memset(au8Key, 0, sizeof(au8Key));
    memset(&uFlashKey, 0, sizeof(uFlashKey));
    if (!bExpParse(E_SL_MSG_OCBEXP_LINK_KEY_REQ, u16Len, OCBEXP_LINK_REQ_LEN,
                   pu8Rx, &u32Transaction))
    {
        return;
    }
    u8Kind = pu8Rx[6];
    u8Index = pu8Rx[7];
    if (!bExpVersion(pu8Rx)) { u8Status = OCBEXP_STATUS_BAD_VERSION; }
    else if (!bExpUnlocked(u32Transaction)) { u8Status = OCBEXP_STATUS_LOCKED; }

    psNib = ZPS_psNwkNibGetHandle(ZPS_pvAplZdoGetNwkHandle());
    psAib = ZPS_psAplAibGetAib();
    if (u8Status == OCBEXP_STATUS_OK && !bExpLayoutValid(psNib, psAib))
    {
        u8Status = OCBEXP_STATUS_LAYOUT_MISMATCH;
    }
    if (u8Status == OCBEXP_STATUS_OK && u8Kind == OCBEXP_KEY_KIND_DEFAULT_TC &&
        u8Index == 0U)
    {
        psKey = psAib->psAplDefaultTCAPSLinkKey;
        u64Eui = psAib->u64ApsTrustCenterAddress;
        u32Incoming = psAib->pu32IncomingFrameCounter[1];
        u32Available = OCBEXP_AVAIL_TC_LINK_KEY | OCBEXP_AVAIL_APS_OUT_COUNTER |
                       OCBEXP_AVAIL_APS_IN_COUNTER | OCBEXP_AVAIL_EUI;
    }
    else if (u8Status == OCBEXP_STATUS_OK &&
             u8Kind == OCBEXP_KEY_KIND_APS_TABLE &&
             u8Index < psAib->psAplDeviceKeyPairTable->u16SizeOfKeyDescriptorTable)
    {
        psKey = &psAib->psAplDeviceKeyPairTable->psAplApsKeyDescriptorEntry[u8Index];
        if (psKey->u16ExtAddrLkup < psNib->sTblSize.u16MacAddTableSize)
        {
            u64Eui = ZPS_u64NwkNibGetMappedIeeeAddr(
                         ZPS_pvAplZdoGetNwkHandle(), psKey->u16ExtAddrLkup);
        }
        u32Incoming = psAib->pu32IncomingFrameCounter[u8Index];
        u32Available = OCBEXP_AVAIL_APS_OUT_COUNTER |
                       OCBEXP_AVAIL_APS_IN_COUNTER;
        if (u64Eui != 0U && u64Eui != 0xFFFFFFFFFFFFFFFFULL)
        {
            u32Available |= OCBEXP_AVAIL_EUI;
        }
    }
    else if (u8Status == OCBEXP_STATUS_OK &&
             u8Kind == OCBEXP_KEY_KIND_FLASH_TCLK &&
             u8Index < OCBEXP_FLASH_TCLK_ENTRIES)
    {
        u64Eui = ZPS_u64GetFlashMappedIeeeAddress(u8Index);
        if (u64Eui != 0U && u64Eui != 0xFFFFFFFFFFFFFFFFULL &&
            zps_bGetFlashCredential(u64Eui, &uFlashKey, &u16CredIndex,
                                    FALSE, FALSE))
        {
            memcpy(au8Key, uFlashKey.au8, 16U);
            u8KeyType = ZPS_u8AplAibGetDeviceApsKeyType(u64Eui);
            u32Available = OCBEXP_AVAIL_TC_LINK_KEY | OCBEXP_AVAIL_EUI;
            /* Per-flash-TCLK APS counters are not exposed by v2395. */
        }
        else
        {
            u8Status = OCBEXP_STATUS_NOT_FOUND;
        }
    }
    else if (u8Status == OCBEXP_STATUS_OK)
    {
        u8Status = OCBEXP_STATUS_NOT_FOUND;
    }

    if (psKey != NULL)
    {
        memcpy(au8Key, psKey->au8LinkKey, 16U);
        u8KeyType = (u64Eui != 0U && u64Eui != 0xFFFFFFFFFFFFFFFFULL) ?
                    ZPS_u8AplAibGetDeviceApsKeyType(u64Eui) :
                    psAib->u8KeyType;
        u32Outgoing = psKey->u32OutgoingFrameCounter;
        u32Available |= OCBEXP_AVAIL_TC_LINK_KEY;
    }

    u8Length = u8ExpPrefix(u32Transaction, u8Status);
    ZNC_BUF_U8_UPD(&s_au8ExpTx[u8Length], u8Kind, u8Length);
    ZNC_BUF_U8_UPD(&s_au8ExpTx[u8Length], u8Index, u8Length);
    ZNC_BUF_U64_UPD(&s_au8ExpTx[u8Length], u64Eui, u8Length);
    ZNC_BUF_U32_UPD(&s_au8ExpTx[u8Length], u32Available, u8Length);
    ZNC_BUF_U8_UPD(&s_au8ExpTx[u8Length], u8KeyType, u8Length);
    memcpy(&s_au8ExpTx[u8Length], au8Key, 16U); u8Length += 16U;
    ZNC_BUF_U32_UPD(&s_au8ExpTx[u8Length], u32Outgoing, u8Length);
    ZNC_BUF_U32_UPD(&s_au8ExpTx[u8Length], u32Incoming, u8Length);
    vSL_WriteMessage(E_SL_MSG_OCBEXP_LINK_KEY_RSP, u8Length, s_au8ExpTx, 0);
    vExpWipe(au8Key, sizeof(au8Key));
    vExpWipe(&uFlashKey, sizeof(uFlashKey));
    vExpWipe(s_au8ExpTx, sizeof(s_au8ExpTx));
}

PUBLIC void OCBEXP_vHandleRestoreUnavailable(uint16 u16Type, uint16 u16RspType,
                                             uint16 u16Len, const uint8 *pu8Rx)
{
    uint32 u32Transaction;
    uint8 u8Length, u8Status;
    if (!bExpParse(u16Type, u16Len, OCBEXP_REQ_LEN, pu8Rx, &u32Transaction))
    {
        return;
    }
    if (!bExpVersion(pu8Rx)) { u8Status = OCBEXP_STATUS_BAD_VERSION; }
    else if (!bExpUnlocked(u32Transaction)) { u8Status = OCBEXP_STATUS_LOCKED; }
    else { u8Status = OCBEXP_STATUS_RESTORE_UNSUPPORTED; }
    u8Length = u8ExpPrefix(u32Transaction, u8Status);
    ZNC_BUF_U32_UPD(&s_au8ExpTx[u8Length], OCBEXP_LIMIT_BITMAP, u8Length);
    vSL_WriteMessage(u16RspType, u8Length, s_au8ExpTx, 0);
    vExpWipe(s_au8ExpTx, sizeof(s_au8ExpTx));
}

PUBLIC void OCBEXP_vHandleStatus(uint16 u16Len, const uint8 *pu8Rx)
{
    uint32 u32Transaction;
    uint8 u8Length, u8Unlocked, u8Remaining;
    if (!bExpParse(E_SL_MSG_OCBEXP_STATUS_REQ, u16Len, OCBEXP_REQ_LEN,
                   pu8Rx, &u32Transaction))
    {
        return;
    }
    u8Remaining = u8ExpRemainingSeconds(u32Transaction);
    u8Unlocked = (u8Remaining != 0U) ? 1U : 0U;
    u8Length = u8ExpPrefix(u32Transaction,
                           bExpVersion(pu8Rx) ? OCBEXP_STATUS_OK :
                                               OCBEXP_STATUS_BAD_VERSION);
    ZNC_BUF_U8_UPD(&s_au8ExpTx[u8Length], u8Unlocked, u8Length);
    ZNC_BUF_U8_UPD(&s_au8ExpTx[u8Length],
                   u8Remaining, u8Length);
    ZNC_BUF_U32_UPD(&s_au8ExpTx[u8Length], OCBEXP_LIMIT_BITMAP, u8Length);
    vSL_WriteMessage(E_SL_MSG_OCBEXP_STATUS_RSP, u8Length, s_au8ExpTx, 0);
    vExpWipe(s_au8ExpTx, sizeof(s_au8ExpTx));
}

PUBLIC void OCBEXP_vHandleAbort(uint16 u16Len, const uint8 *pu8Rx)
{
    uint32 u32Transaction;
    uint8 u8Length, u8Status;
    if (!bExpParse(E_SL_MSG_OCBEXP_ABORT_REQ, u16Len, OCBEXP_REQ_LEN,
                   pu8Rx, &u32Transaction))
    {
        return;
    }
    u8Status = bExpVersion(pu8Rx) ? OCBEXP_STATUS_OK :
                                    OCBEXP_STATUS_BAD_VERSION;
    vExpLock();
    u8Length = u8ExpPrefix(u32Transaction, u8Status);
    ZNC_BUF_U32_UPD(&s_au8ExpTx[u8Length], OCBEXP_LIMIT_BITMAP, u8Length);
    vSL_WriteMessage(E_SL_MSG_OCBEXP_ABORT_RSP, u8Length, s_au8ExpTx, 0);
    vExpWipe(s_au8ExpTx, sizeof(s_au8ExpTx));
}
