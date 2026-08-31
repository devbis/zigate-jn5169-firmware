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
/* Restore-path statuses for 0x0D24..0x0D28. Status 5 was RESTORE_UNSUPPORTED
 * while the restore opcodes were non-mutating stubs; the experimental restore
 * now implements those ops, so 5 is repurposed as NO_SESSION. Values 0..4 keep
 * their export-path meaning so SECRET_CORE/LINK_KEY wire behaviour is unchanged.
 * This remains an EXPERIMENTAL, unqualified path: it is NOT authenticated, has
 * no atomic rollback, and never sets the reserved BackupCapable bit 17. */
#define OCBEXP_STATUS_NO_SESSION           (5U)  /* restore op with no RESTORE_BEGIN */
#define OCBEXP_STATUS_INCOMPLETE           (6U)  /* VALIDATE/COMMIT before mandatory fields present */
#define OCBEXP_STATUS_BAD_FIELD            (7U)  /* recognised field id with a wrong value length */

/* No secret: host confirms deliberate use with
 * nonce XOR transaction_id XOR OCBEXP_CONFIRM_MAGIC. */
#define OCBEXP_CONFIRM_MAGIC               (0x4F434221UL) /* "OCB!" */
#define OCBEXP_UNLOCK_SECONDS              (30U)
/* The unlock deadline is a dedicated ZTimer (u8OcbUnlockTimer, opened in
 * app_start.c), NOT raw u32AHI_TickTimerRead() arithmetic: ZTimer.c already
 * reprograms that same AHI hardware Tick Timer into a periodic 1 ms restart
 * mode as its own software-timer tick source (vAHI_TickTimerInterval(16000)
 * at 16 MHz), so the register does not behave as a free-running 32-bit
 * counter and a raw elapsed computation aliases roughly every millisecond. */

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
/* Coordinator IEEE adoption is IMPLEMENTED, not merely "unsafe" -- but this bit
 * stays set as a standing caution flag, not a "broken" marker. History: HIL
 * testing first found ZPS_vSetOverrideLocalIeeeAddr() reliably hung boot when
 * called before ZPS_eAplAfInit(); root-caused by disassembling
 * libZPSMAC_Mini_SOC_JN516x.a / libMiniMac_JN5169.a to a hardware MAC register
 * write reached before the radio was clocked/initialised. Fixed by moving the
 * call to after ZPS_eAplAfInit() (see OCBEXP_vApplyAdoptedIeeeAtBoot() in
 * ocb_experimental.c); HIL-verified 3/3 reliable restore-reboot-verify cycles
 * afterward. What remains untested: every HIL run so far has been a
 * self-restore on one physical unit (source IEEE == target IEEE), never a
 * true two-device migration with a genuinely different target IEEE, and never
 * against real paired end devices that would need to accept the identity
 * change. It also still mutates the live MAC extended address -- running two
 * units with the same adopted IEEE on one network is unsafe by construction,
 * independent of firmware correctness. Treat as freshly re-verified, not
 * long-proven. */
#define OCBEXP_LIMIT_IEEE_OVERRIDE_UNSAFE  (1UL << 4)
/* bit 5 reserved (was OCBEXP_LIMIT_FC_NOT_RESTORED). No longer set: the NWK
 * outgoing frame counter IS restorable. Root-caused by disassembling
 * libZPSNWK_JN516x.a -- ZPS_vSaveAllZpsRecords()/ZPS_vNwkSaveSecMat() never
 * carried it because the SDK does not persist sTbl.u32OutFC as a plain value
 * at all; it reconstructs it at boot as `bitmap_value << shift` from
 * PDM_ID_INTERNAL_NWK_OUT_FC_BITMAP, a PDM *bitmap* record whose value is a
 * pure popcount of PDM_eIncrementBitmap() calls (confirmed via disassembly of
 * vIncrementFrameCounterInPdm/ZPS_pvNwkRestoreFrameCounter and an on-device
 * readback). The fix deletes and recreates that bitmap, then writes
 * ceil(target/block) via ePDM_SetBitmapToValue() in one shot -- HIL-verified
 * the restored counter is reconstructed higher than the backed-up value
 * after a real reboot. O(1) regardless of magnitude (see the field handler
 * in ocb_experimental.c for why this replaced an O(N^2)
 * PDM_eIncrementBitmap() loop), so no step cap is needed. */
#define OCBEXP_LIMIT_BITMAP                \
    (OCBEXP_LIMIT_NO_AUTH_OR_ENCRYPTION | OCBEXP_LIMIT_FLASH_TCLK_COUNTERS | \
     OCBEXP_LIMIT_NO_ATOMIC_ROLLBACK | OCBEXP_LIMIT_RESTORE_UNQUALIFIED | \
     OCBEXP_LIMIT_IEEE_OVERRIDE_UNSAFE)

/* --- Experimental streamed restore (0x0D24..0x0D28) -------------------------
 *
 * The host must CHALLENGE/UNLOCK first, exactly as for key export. Restore is a
 * field-tagged (TLV) stream so an image built by a different firmware/PDM
 * revision skips unknown field ids instead of corrupting layout:
 *
 *   RESTORE_BEGIN  (0x0D24) start a session and report restore capabilities.
 *   RESTORE_FIELD  (0x0D25) repeatable: {field_id:u16, length:u16, value[]}.
 *                          Unknown field ids are acknowledged as SKIPPED.
 *   RESTORE_LINK   (0x0D26) repeatable: one per-device link key.
 *   VALIDATE       (0x0D27) no new writes; report present-bitmap + mandatory_ok.
 *   COMMIT         (0x0D28) persist (ZPS_vSaveAllZpsRecords) and reboot.
 *
 * Fields are applied straight into the live NIB/AIB as they arrive; nothing is
 * persisted until COMMIT. ABORT (or any power cycle before COMMIT) discards the
 * in-RAM changes. There is no atomic rollback across COMMIT: keep the unit
 * powered throughout it. */

/* Restore field-carrier framing. RESTORE_FIELD is variable length and must NOT
 * use bExpParse's exact-length check. */
#define OCBEXP_FIELD_HDR_LEN               (4U)   /* field_id:u16 + length:u16 */
#define OCBEXP_RESTORE_FIELD_MIN_LEN       (OCBEXP_REQ_LEN + OCBEXP_FIELD_HDR_LEN) /* 10 */
#define OCBEXP_RESTORE_LINK_REQ_LEN        (31U)  /* common6 + eui8 + type1 + key16 */

/* Replay-protection headroom added to the restored NWK outgoing frame counter so
 * a re-formed coordinator never emits a frame counter a peer already accepted. */
#define OCBEXP_NWK_OUTFC_MARGIN            (0x400UL)

/* PDM_ID_INTERNAL_NWK_OUT_FC_BITMAP's persisted value is a popcount written
 * directly via ePDM_SetBitmapToValue() (see OCBEXP_FIELD_NWK_OUT_FC in
 * ocb_experimental.c) -- O(1) regardless of magnitude, so no step cap. */

/* Restore field ids (u16, big-endian on the wire). */
#define OCBEXP_FIELD_NWK_KEY               (0x0001U) /* 16 bytes */
#define OCBEXP_FIELD_NWK_KEY_SEQ           (0x0002U) /* 1 byte  */
#define OCBEXP_FIELD_NWK_OUT_FC            (0x0003U) /* 4 bytes */
#define OCBEXP_FIELD_PAN_ID                (0x0004U) /* 2 bytes */
#define OCBEXP_FIELD_EXT_PAN_ID            (0x0005U) /* 8 bytes */
#define OCBEXP_FIELD_CHANNEL               (0x0006U) /* 1 byte  */
#define OCBEXP_FIELD_NWK_ADDR              (0x0007U) /* 2 bytes */
#define OCBEXP_FIELD_NWK_UPDATE_ID         (0x0008U) /* 1 byte  */
#define OCBEXP_FIELD_TC_ADDR               (0x0009U) /* 8 bytes */
#define OCBEXP_FIELD_TC_LINK_KEY           (0x000AU) /* 16 bytes */
#define OCBEXP_FIELD_TC_KEY_TYPE           (0x000BU) /* 1 byte  */
/* 8 bytes; recognised but always refused with OCBEXP_FIELD_UNAVAILABLE. HIL
 * testing found ZPS_vSetOverrideLocalIeeeAddr() reliably hangs boot on this
 * hardware; see OCBEXP_vApplyAdoptedIeeeAtBoot() in ocb_experimental.c. */
#define OCBEXP_FIELD_ADOPT_IEEE            (0x000CU)

/* Per-field apply result reported in the RESTORE_FIELD/RESTORE_LINK response. */
#define OCBEXP_FIELD_APPLIED               (0U)
#define OCBEXP_FIELD_SKIPPED_UNKNOWN       (1U)
#define OCBEXP_FIELD_BAD_LENGTH            (2U)
#define OCBEXP_FIELD_UNAVAILABLE           (3U) /* recognised, but layout blocked apply */

/* Present-bitmap tracked across the session (returned by VALIDATE). */
#define OCBEXP_PRESENT_NWK_KEY             (1UL << 0)
#define OCBEXP_PRESENT_NWK_KEY_SEQ         (1UL << 1)
#define OCBEXP_PRESENT_NWK_OUT_FC          (1UL << 2)
#define OCBEXP_PRESENT_PAN_ID              (1UL << 3)
#define OCBEXP_PRESENT_EXT_PAN_ID          (1UL << 4)
#define OCBEXP_PRESENT_CHANNEL             (1UL << 5)
#define OCBEXP_PRESENT_NWK_ADDR            (1UL << 6)
#define OCBEXP_PRESENT_NWK_UPDATE_ID       (1UL << 7)
#define OCBEXP_PRESENT_TC_ADDR             (1UL << 8)
#define OCBEXP_PRESENT_TC_LINK_KEY         (1UL << 9)
#define OCBEXP_PRESENT_TC_KEY_TYPE         (1UL << 10)
#define OCBEXP_PRESENT_ADOPT_IEEE          (1UL << 11)
#define OCBEXP_PRESENT_LINK_KEY            (1UL << 12) /* at least one RESTORE_LINK applied */

/* Minimum set required before VALIDATE/COMMIT will proceed. */
#define OCBEXP_PRESENT_MANDATORY \
    (OCBEXP_PRESENT_NWK_KEY | OCBEXP_PRESENT_NWK_KEY_SEQ | OCBEXP_PRESENT_PAN_ID | \
     OCBEXP_PRESENT_EXT_PAN_ID | OCBEXP_PRESENT_CHANNEL | OCBEXP_PRESENT_NWK_ADDR)

/* Restore capabilities advertised in the RESTORE_BEGIN response (distinct from
 * the export OCBEXP_LIMIT_* bitmap). */
#define OCBEXP_RCAP_NWK_KEY                (1UL << 0)
#define OCBEXP_RCAP_IDENTITY               (1UL << 1)
#define OCBEXP_RCAP_LINK_KEYS              (1UL << 2)
#define OCBEXP_RCAP_IEEE_ADOPT             (1UL << 3)
#define OCBEXP_RCAP_BITMAP \
    (OCBEXP_RCAP_NWK_KEY | OCBEXP_RCAP_IDENTITY | OCBEXP_RCAP_LINK_KEYS | \
     OCBEXP_RCAP_IEEE_ADOPT)

PUBLIC void OCBEXP_vHandleChallenge(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void OCBEXP_vHandleUnlock(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void OCBEXP_vHandleSecretCore(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void OCBEXP_vHandleLinkKey(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void OCBEXP_vHandleRestoreBegin(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void OCBEXP_vHandleRestoreField(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void OCBEXP_vHandleRestoreLink(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void OCBEXP_vHandleValidate(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void OCBEXP_vHandleCommit(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void OCBEXP_vHandleStatus(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void OCBEXP_vHandleAbort(uint16 u16Len, const uint8 *pu8Rx);
/* Applied at boot (app_start.c) before ZPS_eAplAfInit() when a prior restore
 * staged an adopted coordinator IEEE. No-op if none is staged. */
PUBLIC void OCBEXP_vApplyAdoptedIeeeAtBoot(void);
/* ZTimer callback (app_start.c opens u8OcbUnlockTimer against this) firing
 * OCBEXP_UNLOCK_SECONDS after the last CHALLENGE/UNLOCK; ends the unlock. */
PUBLIC void OCBEXP_vUnlockTimeout(void *pvParam);

#endif
