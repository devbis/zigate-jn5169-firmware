/****************************************************************************
 *
 * EXPERIMENTAL Open Coordinator Backup key export over trusted local UART.
 *
 * The nonce confirmation is an accidental-invocation guard, NOT
 * authentication. It contains no secret and provides no confidentiality.
 *
 ****************************************************************************/
#ifndef OCB_EXPERIMENTAL_H_
#define OCB_EXPERIMENTAL_H_

#include <jendefs.h>

#define OCBEXP_ABI_VERSION                 (1U)
#define OCBEXP_SCHEMA_VERSION              (1U)

#define E_SL_MSG_OCBEXP_CHALLENGE_REQ      (0x0D20U)
#define E_SL_MSG_OCBEXP_CHALLENGE_RSP      (0x8D20U)
#define E_SL_MSG_OCBEXP_UNLOCK_REQ         (0x0D21U)
#define E_SL_MSG_OCBEXP_UNLOCK_RSP         (0x8D21U)
#define E_SL_MSG_OCBEXP_SECRET_CORE_REQ    (0x0D22U)
#define E_SL_MSG_OCBEXP_SECRET_CORE_RSP    (0x8D22U)
#define E_SL_MSG_OCBEXP_LINK_KEY_REQ       (0x0D23U)
#define E_SL_MSG_OCBEXP_LINK_KEY_RSP       (0x8D23U)
#define E_SL_MSG_OCBEXP_RESTORE_BEGIN_REQ  (0x0D24U)
#define E_SL_MSG_OCBEXP_RESTORE_BEGIN_RSP  (0x8D24U)
#define E_SL_MSG_OCBEXP_RESTORE_CORE_REQ   (0x0D25U)
#define E_SL_MSG_OCBEXP_RESTORE_CORE_RSP   (0x8D25U)
#define E_SL_MSG_OCBEXP_RESTORE_LINK_REQ   (0x0D26U)
#define E_SL_MSG_OCBEXP_RESTORE_LINK_RSP   (0x8D26U)
#define E_SL_MSG_OCBEXP_VALIDATE_REQ       (0x0D27U)
#define E_SL_MSG_OCBEXP_VALIDATE_RSP       (0x8D27U)
#define E_SL_MSG_OCBEXP_COMMIT_REQ         (0x0D28U)
#define E_SL_MSG_OCBEXP_COMMIT_RSP         (0x8D28U)
#define E_SL_MSG_OCBEXP_STATUS_REQ         (0x0D29U)
#define E_SL_MSG_OCBEXP_STATUS_RSP         (0x8D29U)
#define E_SL_MSG_OCBEXP_ABORT_REQ          (0x0D2AU)
#define E_SL_MSG_OCBEXP_ABORT_RSP          (0x8D2AU)

#define OCBEXP_REQ_LEN                     (6U)
#define OCBEXP_UNLOCK_REQ_LEN              (14U)
#define OCBEXP_LINK_REQ_LEN                (8U)
#define OCBEXP_CHALLENGE_RSP_LEN           (16U)
#define OCBEXP_UNLOCK_RSP_LEN              (12U)
#define OCBEXP_SECRET_CORE_RSP_LEN         (61U)
#define OCBEXP_LINK_RSP_LEN                (46U)
#define OCBEXP_STATUS_RSP_LEN              (13U)

#define OCBEXP_STATUS_OK                   (0U)
#define OCBEXP_STATUS_BAD_VERSION          (1U)
#define OCBEXP_STATUS_LOCKED               (2U)
#define OCBEXP_STATUS_NOT_FOUND            (3U)
#define OCBEXP_STATUS_LAYOUT_MISMATCH       (4U)
#define OCBEXP_STATUS_RESTORE_UNSUPPORTED   (5U)

/* No secret: host confirms deliberate use with
 * nonce XOR transaction_id XOR OCBEXP_CONFIRM_MAGIC. */
#define OCBEXP_CONFIRM_MAGIC               (0x4F434221UL) /* "OCB!" */
#define OCBEXP_UNLOCK_SECONDS              (30U)
#define OCBEXP_TICK_HZ                     (16000000UL)
#define OCBEXP_UNLOCK_TICKS                (OCBEXP_TICK_HZ * OCBEXP_UNLOCK_SECONDS)

#define OCBEXP_AVAIL_NWK_KEY               (1UL << 0)
#define OCBEXP_AVAIL_NWK_OUT_COUNTER       (1UL << 1)
#define OCBEXP_AVAIL_TC_LINK_KEY           (1UL << 2)
#define OCBEXP_AVAIL_APS_OUT_COUNTER       (1UL << 3)
#define OCBEXP_AVAIL_APS_IN_COUNTER        (1UL << 4)
#define OCBEXP_AVAIL_EUI                   (1UL << 5)

#define OCBEXP_KEY_KIND_DEFAULT_TC         (0U)
#define OCBEXP_KEY_KIND_APS_TABLE          (1U)
#define OCBEXP_KEY_KIND_FLASH_TCLK         (2U)

/* Precise blockers to production Backup/Restore capability. */
#define OCBEXP_LIMIT_NO_AUTH_OR_ENCRYPTION (1UL << 0)
#define OCBEXP_LIMIT_FLASH_TCLK_COUNTERS   (1UL << 1)
#define OCBEXP_LIMIT_NO_ATOMIC_ROLLBACK    (1UL << 2)
#define OCBEXP_LIMIT_RESTORE_UNQUALIFIED   (1UL << 3)
#define OCBEXP_LIMIT_IEEE_OVERRIDE_UNSAFE  (1UL << 4)
#define OCBEXP_LIMIT_BITMAP                \
    (OCBEXP_LIMIT_NO_AUTH_OR_ENCRYPTION | OCBEXP_LIMIT_FLASH_TCLK_COUNTERS | \
     OCBEXP_LIMIT_NO_ATOMIC_ROLLBACK | OCBEXP_LIMIT_RESTORE_UNQUALIFIED | \
     OCBEXP_LIMIT_IEEE_OVERRIDE_UNSAFE)

PUBLIC void OCBEXP_vHandleChallenge(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void OCBEXP_vHandleUnlock(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void OCBEXP_vHandleSecretCore(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void OCBEXP_vHandleLinkKey(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void OCBEXP_vHandleRestoreUnavailable(uint16 u16Type, uint16 u16RspType,
                                             uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void OCBEXP_vHandleStatus(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void OCBEXP_vHandleAbort(uint16 u16Len, const uint8 *pu8Rx);

#endif
