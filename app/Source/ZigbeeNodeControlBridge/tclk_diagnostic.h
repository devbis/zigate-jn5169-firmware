#ifndef TCLK_DIAGNOSTIC_H_
#define TCLK_DIAGNOSTIC_H_

#include <stddef.h>
#include "jendefs.h"
#include "zps_apl.h"

#define TCLK_DIAGNOSTIC_MAGIC   (0x54434c4bUL) /* "TCLK" */
#define TCLK_DIAGNOSTIC_VERSION (0x0002U)
#define TCLK_DIAGNOSTIC_U8_NA   (0xffU)
#define TCLK_DIAGNOSTIC_U16_NA  (0xffffU)
#define TCLK_DIAGNOSTIC_U32_NA  (0xffffffffUL)

/*
 * Volatile, RAM-only state intended for JTAG reads. No key bytes are retained.
 * Sequence values are odd while a record is being updated and even when stable.
 */
typedef struct
{
    uint32 u32Magic;                    /* 0x00 */
    uint16 u16Version;                  /* 0x04 */
    uint16 u16Size;                     /* 0x06 */
    uint64 u64CallbackDeviceIeee;       /* 0x08 */
    uint32 u32CallbackSequence;         /* 0x10 */
    uint16 u16CallbackShortAddress;     /* 0x14 */
    uint16 u16CallbackMacId;            /* 0x16 */
    uint8  u8CallbackStatus;            /* 0x18 */
    uint8  u8FlashFeature;              /* 0x19 */
    uint8  u8InsertionAttempted;        /* 0x1a */
    uint8  u8AddReplaceStatus;          /* 0x1b */
    uint8  u8CredentialPresent;         /* 0x1c */
    uint8  u8CallbackReturn;            /* 0x1d */
    uint16 u16CredentialIndex;          /* 0x1e */

    uint64 u64ConfirmDeviceIeee;        /* 0x20 */
    uint32 u32ConfirmSequence;          /* 0x28 */
    uint16 u16NwkLookupFirst;           /* 0x2c */
    uint16 u16NwkLookupSecond;          /* 0x2e */
    uint32 u32VerifyValidationSequence; /* 0x30 */
    uint8  u8ConfirmInputStatus;        /* 0x34 */
    uint8  u8ConfirmKeyType;            /* 0x35 */
    uint8  u8NwkLookupCount;            /* 0x36 */
    uint8  u8EncryptionObserved;        /* 0x37 */
    uint8  u8EncryptionReturnStatus;    /* 0x38 */
    uint8  u8EncryptionSecured;         /* 0x39 */
    uint8  u8ConfirmReturnStatus;       /* 0x3a */
    uint8  u8SendReturnStatusEquivalent; /* 0x3b */
    uint32 u32VerifyValidationAtConfirm; /* 0x3c */
} TCLKDIAG_tsState;

/* Fail at compile time if this BA2 ABI layout changes. */
#define TCLKDIAG_OFFSET_ASSERT(member, offset) \
    typedef char tclkdiag_offset_##member[(offsetof(TCLKDIAG_tsState, member) == (offset)) ? 1 : -1]
TCLKDIAG_OFFSET_ASSERT(u32Magic, 0x00);
TCLKDIAG_OFFSET_ASSERT(u64CallbackDeviceIeee, 0x08);
TCLKDIAG_OFFSET_ASSERT(u32CallbackSequence, 0x10);
TCLKDIAG_OFFSET_ASSERT(u16CredentialIndex, 0x1e);
TCLKDIAG_OFFSET_ASSERT(u64ConfirmDeviceIeee, 0x20);
TCLKDIAG_OFFSET_ASSERT(u32ConfirmSequence, 0x28);
TCLKDIAG_OFFSET_ASSERT(u32VerifyValidationSequence, 0x30);
TCLKDIAG_OFFSET_ASSERT(u8SendReturnStatusEquivalent, 0x3b);
TCLKDIAG_OFFSET_ASSERT(u32VerifyValidationAtConfirm, 0x3c);
typedef char tclkdiag_size_assert[(sizeof(TCLKDIAG_tsState) == 0x40) ? 1 : -1];

PUBLIC extern volatile TCLKDIAG_tsState g_sTclkDiagnostic;

PUBLIC bool_t TCLKDIAG_bSnapshot(uint8 *pu8Buffer);
PUBLIC void TCLKDIAG_vCallbackBegin(uint16 u16ShortAddress,
                                    uint64 u64DeviceAddress,
                                    uint8 u8Status,
                                    uint16 u16MacId,
                                    bool_t bFlashFeature);
PUBLIC void TCLKDIAG_vInsertionAttempted(void);
PUBLIC void TCLKDIAG_vInsertionResult(ZPS_teStatus eAddReplaceStatus,
                                     bool_t bCredentialPresent,
                                     uint16 u16CredentialIndex);
PUBLIC void TCLKDIAG_vCallbackEnd(bool_t bCallbackReturn);

#endif /* TCLK_DIAGNOSTIC_H_ */
