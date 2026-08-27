#include "tclk_diagnostic.h"

#include "zps_apl_af.h"
#include "zps_apl_aib.h"
#include "zps_nwk_nib.h"

PUBLIC volatile TCLKDIAG_tsState g_sTclkDiagnostic
    __attribute__((used, aligned(4))) =
{
    .u32Magic = TCLK_DIAGNOSTIC_MAGIC,
    .u16Version = TCLK_DIAGNOSTIC_VERSION,
    .u16Size = sizeof(TCLKDIAG_tsState),
    .u8AddReplaceStatus = TCLK_DIAGNOSTIC_U8_NA,
    .u16CredentialIndex = TCLK_DIAGNOSTIC_U16_NA,
    .u16NwkLookupFirst = TCLK_DIAGNOSTIC_U16_NA,
    .u16NwkLookupSecond = TCLK_DIAGNOSTIC_U16_NA,
    .u8EncryptionReturnStatus = TCLK_DIAGNOSTIC_U8_NA,
    .u8EncryptionSecured = TCLK_DIAGNOSTIC_U8_NA,
    .u8ConfirmReturnStatus = TCLK_DIAGNOSTIC_U8_NA,
    .u8SendReturnStatusEquivalent = TCLK_DIAGNOSTIC_U8_NA,
    .u32VerifyValidationAtConfirm = TCLK_DIAGNOSTIC_U32_NA
};

PRIVATE volatile bool_t bConfirmInProgress = FALSE;

PUBLIC bool_t TCLKDIAG_bSnapshot(uint8 *pu8Buffer)
{
    const volatile uint8 *pu8Source =
        (const volatile uint8 *)&g_sTclkDiagnostic;
    uint32 u32CallbackBefore;
    uint32 u32CallbackAfter;
    uint32 u32ConfirmBefore;
    uint32 u32ConfirmAfter;
    uint32 u32ValidationBefore;
    uint32 u32ValidationAfter;
    uint8 u8Attempt;
    uint8 u8Index;

    if (pu8Buffer == NULL)
    {
        return FALSE;
    }

    for (u8Attempt = 0; u8Attempt < 3; u8Attempt++)
    {
        u32CallbackBefore = g_sTclkDiagnostic.u32CallbackSequence;
        u32ConfirmBefore = g_sTclkDiagnostic.u32ConfirmSequence;
        u32ValidationBefore =
            g_sTclkDiagnostic.u32VerifyValidationSequence;
        if ((u32CallbackBefore & 1U) != 0 ||
            (u32ConfirmBefore & 1U) != 0 ||
            (u32ValidationBefore & 1U) != 0)
        {
            continue;
        }

        for (u8Index = 0; u8Index < sizeof(TCLKDIAG_tsState); u8Index++)
        {
            pu8Buffer[u8Index] = pu8Source[u8Index];
        }

        u32CallbackAfter = g_sTclkDiagnostic.u32CallbackSequence;
        u32ConfirmAfter = g_sTclkDiagnostic.u32ConfirmSequence;
        u32ValidationAfter =
            g_sTclkDiagnostic.u32VerifyValidationSequence;
        if (u32CallbackBefore == u32CallbackAfter &&
            u32ConfirmBefore == u32ConfirmAfter &&
            u32ValidationBefore == u32ValidationAfter &&
            (u32CallbackAfter & 1U) == 0 &&
            (u32ConfirmAfter & 1U) == 0 &&
            (u32ValidationAfter & 1U) == 0)
        {
            return TRUE;
        }
    }

    return FALSE;
}

PUBLIC void TCLKDIAG_vCallbackBegin(uint16 u16ShortAddress,
                                    uint64 u64DeviceAddress,
                                    uint8 u8Status,
                                    uint16 u16MacId,
                                    bool_t bFlashFeature)
{
    g_sTclkDiagnostic.u32CallbackSequence++;
    g_sTclkDiagnostic.u64CallbackDeviceIeee = u64DeviceAddress;
    g_sTclkDiagnostic.u16CallbackShortAddress = u16ShortAddress;
    g_sTclkDiagnostic.u16CallbackMacId = u16MacId;
    g_sTclkDiagnostic.u8CallbackStatus = u8Status;
    g_sTclkDiagnostic.u8FlashFeature = bFlashFeature;
    g_sTclkDiagnostic.u8InsertionAttempted = FALSE;
    g_sTclkDiagnostic.u8AddReplaceStatus = TCLK_DIAGNOSTIC_U8_NA;
    g_sTclkDiagnostic.u8CredentialPresent = FALSE;
    g_sTclkDiagnostic.u8CallbackReturn = TCLK_DIAGNOSTIC_U8_NA;
    g_sTclkDiagnostic.u16CredentialIndex = TCLK_DIAGNOSTIC_U16_NA;
}

PUBLIC void TCLKDIAG_vInsertionAttempted(void)
{
    g_sTclkDiagnostic.u8InsertionAttempted = TRUE;
}

PUBLIC void TCLKDIAG_vInsertionResult(ZPS_teStatus eAddReplaceStatus,
                                     bool_t bCredentialPresent,
                                     uint16 u16CredentialIndex)
{
    g_sTclkDiagnostic.u8AddReplaceStatus = eAddReplaceStatus;
    g_sTclkDiagnostic.u8CredentialPresent = bCredentialPresent;
    g_sTclkDiagnostic.u16CredentialIndex =
        bCredentialPresent ? u16CredentialIndex : TCLK_DIAGNOSTIC_U16_NA;
}

PUBLIC void TCLKDIAG_vCallbackEnd(bool_t bCallbackReturn)
{
    g_sTclkDiagnostic.u8CallbackReturn = bCallbackReturn;
    g_sTclkDiagnostic.u32CallbackSequence++;
}

/*
 * These wrappers are linker-supported interposition only. They return every
 * original value unchanged and are active solely while Confirm-Key executes.
 */
extern ZPS_teStatus __real_zps_eAplApsmeConfirmKeyReqRsp(void *pvApl,
                                                         uint8 u8Status,
                                                         uint64 u64DstAddr,
                                                         uint8 u8KeyType);

PUBLIC ZPS_teStatus __wrap_zps_eAplApsmeConfirmKeyReqRsp(void *pvApl,
                                                         uint8 u8Status,
                                                         uint64 u64DstAddr,
                                                         uint8 u8KeyType)
{
    ZPS_teStatus eStatus;

    g_sTclkDiagnostic.u32ConfirmSequence++;
    g_sTclkDiagnostic.u64ConfirmDeviceIeee = u64DstAddr;
    g_sTclkDiagnostic.u16NwkLookupFirst = TCLK_DIAGNOSTIC_U16_NA;
    g_sTclkDiagnostic.u16NwkLookupSecond = TCLK_DIAGNOSTIC_U16_NA;
    g_sTclkDiagnostic.u32VerifyValidationAtConfirm =
        g_sTclkDiagnostic.u32VerifyValidationSequence;
    g_sTclkDiagnostic.u8ConfirmInputStatus = u8Status;
    g_sTclkDiagnostic.u8ConfirmKeyType = u8KeyType;
    g_sTclkDiagnostic.u8NwkLookupCount = 0;
    g_sTclkDiagnostic.u8EncryptionObserved = FALSE;
    g_sTclkDiagnostic.u8EncryptionReturnStatus = TCLK_DIAGNOSTIC_U8_NA;
    g_sTclkDiagnostic.u8EncryptionSecured = TCLK_DIAGNOSTIC_U8_NA;
    g_sTclkDiagnostic.u8ConfirmReturnStatus = TCLK_DIAGNOSTIC_U8_NA;
    g_sTclkDiagnostic.u8SendReturnStatusEquivalent = TCLK_DIAGNOSTIC_U8_NA;

    bConfirmInProgress = TRUE;
    eStatus = __real_zps_eAplApsmeConfirmKeyReqRsp(pvApl,
                                                   u8Status,
                                                   u64DstAddr,
                                                   u8KeyType);
    bConfirmInProgress = FALSE;

    g_sTclkDiagnostic.u8ConfirmReturnStatus = eStatus;
    /*
     * Exact-library disassembly proves Confirm-Key returns SendApsmeCmdFrame's
     * return unchanged whenever Send is entered. The IEEE->NWK lookup is the
     * first observable operation unique to that internal Send path.
     */
    if (g_sTclkDiagnostic.u8NwkLookupCount != 0)
    {
        g_sTclkDiagnostic.u8SendReturnStatusEquivalent = eStatus;
    }
    g_sTclkDiagnostic.u32ConfirmSequence++;
    return eStatus;
}

/*
 * Exact-library DWARF gives this four-argument ABI. Only an odd/even sequence
 * is retained; no input, output hash, key bytes, or addresses are recorded.
 */
extern void __real_zps_vGenerateHashForVerifiedKey(void *pvApl,
                                                   void *pvInput,
                                                   uint8 u8KeyType,
                                                   void *pvOutput);

PUBLIC void __wrap_zps_vGenerateHashForVerifiedKey(void *pvApl,
                                                   void *pvInput,
                                                   uint8 u8KeyType,
                                                   void *pvOutput)
{
    g_sTclkDiagnostic.u32VerifyValidationSequence++;
    __real_zps_vGenerateHashForVerifiedKey(pvApl,
                                           pvInput,
                                           u8KeyType,
                                           pvOutput);
    g_sTclkDiagnostic.u32VerifyValidationSequence++;
}

extern uint16 __real_ZPS_u16NwkNibFindNwkAddr(void *pvNwk,
                                              uint64 u64ExtAddr);

PUBLIC uint16 __wrap_ZPS_u16NwkNibFindNwkAddr(void *pvNwk,
                                              uint64 u64ExtAddr)
{
    uint16 u16Address = __real_ZPS_u16NwkNibFindNwkAddr(pvNwk, u64ExtAddr);

    if (bConfirmInProgress)
    {
        if (g_sTclkDiagnostic.u8NwkLookupCount == 0)
        {
            g_sTclkDiagnostic.u16NwkLookupFirst = u16Address;
        }
        else if (g_sTclkDiagnostic.u8NwkLookupCount == 1)
        {
            g_sTclkDiagnostic.u16NwkLookupSecond = u16Address;
        }
        if (g_sTclkDiagnostic.u8NwkLookupCount != TCLK_DIAGNOSTIC_U8_NA)
        {
            g_sTclkDiagnostic.u8NwkLookupCount++;
        }
    }
    return u16Address;
}

/*
 * DWARF in the exact library gives this six-argument ABI. Interposing the
 * encryption entry point is safe because zps_apl_apsme.o references it as an
 * undefined symbol. The descriptor lookup itself is defined and called inside
 * zps_apl_sec.o, so GNU ld --wrap cannot intercept that one internal call.
 */
extern ZPS_teStatus __real_zps_eAplSecEncryptPacket(void *pvApl,
                                                    void *pvNPdu,
                                                    uint16 u16NwkDstAddr,
                                                    uint8 u8KeyId,
                                                    bool_t bExtNonce,
                                                    bool_t *pbSecured);

PUBLIC ZPS_teStatus __wrap_zps_eAplSecEncryptPacket(void *pvApl,
                                                    void *pvNPdu,
                                                    uint16 u16NwkDstAddr,
                                                    uint8 u8KeyId,
                                                    bool_t bExtNonce,
                                                    bool_t *pbSecured)
{
    bool_t bCapture = bConfirmInProgress;
    ZPS_teStatus eStatus;

    if (bCapture)
    {
        g_sTclkDiagnostic.u8EncryptionObserved = TRUE;
    }
    eStatus = __real_zps_eAplSecEncryptPacket(pvApl,
                                              pvNPdu,
                                              u16NwkDstAddr,
                                              u8KeyId,
                                              bExtNonce,
                                              pbSecured);
    if (bCapture)
    {
        g_sTclkDiagnostic.u8EncryptionReturnStatus = eStatus;
        g_sTclkDiagnostic.u8EncryptionSecured =
            (pbSecured != NULL) ? *pbSecured : TCLK_DIAGNOSTIC_U8_NA;
    }
    return eStatus;
}
