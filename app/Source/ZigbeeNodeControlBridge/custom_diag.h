/****************************************************************************
 *
 * MODULE:  custom_diag.h
 *
 * DESCRIPTION:
 *   Compact, versioned, read-only UART diagnostic extension for the ZiGate
 *   JN5169 ZigbeeNodeControlBridge coordinator firmware.
 *
 *   This module is strictly additive. It does not modify, replace or expand
 *   any stock SerialLink command, and it never exposes key material, allows
 *   mutation of network identity, or performs raw PDM / credential-flash
 *   access. Only the following capabilities are implemented and advertised:
 *
 *     - Capability negotiation           (0x0D0F / 0x8D0F)
 *     - General read-only diagnostics    (0x0D1F / 0x8D1F)
 *     - Paginated local neighbour table  (0x0D14 / 0x8D14)
 *     - Paginated local route table      (0x0D15 / 0x8D15)
 *     - Local APS group operation        (0x0D12 / 0x8D12)
 *     - Local APS group paginated list   (0x0D13 / 0x8D13)
 *
 *   All wire fields are serialised individually in big-endian order using the
 *   ZNC_BUF_* macros. No C structure is ever cast onto the wire, so the wire
 *   ABI is independent of BA GCC 4.7.4 struct layout.
 *
 *   IMPORTANT: vSL_WriteMessage() always appends one link-quality byte to the
 *   caller supplied buffer beyond the declared payload length. Every response
 *   buffer therefore reserves one extra writable byte (DIAG_TX_LQI_RESERVE).
 *
 ****************************************************************************/

#ifndef CUSTOM_DIAG_H_
#define CUSTOM_DIAG_H_

#include <jendefs.h>

/****************************************************************************/
/***        Protocol / capability constants                               ***/
/****************************************************************************/

/* Custom diagnostic protocol version (independent of the stock firmware
 * version). Bump the minor for backward-compatible field additions and the
 * major for incompatible wire changes.
 *
 * 1.1 revises paginated responses (7-byte prefix with an explicit next_index
 * cursor) and adds a physical table index to each group-list row, so it is not
 * wire-compatible with the never-released 1.0 draft. */
#define DIAG_PROTO_MAJOR                (1U)
#define DIAG_PROTO_MINOR                (1U)

/* Explicit deterministic build revision. MUST be incremented whenever the
 * implementation changes in a way that should be observable through the
 * capability build id, even if version/protocol/capabilities are unchanged.
 *   rev 1: initial custom protocol.
 *   rev 2: NXP review fixes (TX power PIB handling, 0x8806 gating, 0xC4
 *          formation-failure status, group pagination index + next cursor,
 *          neighbour IEEE guard, endpoint-state validation, TCLK snapshot,
 *          reserved-flag rejection).
 *   rev 3: HIL fix - stock 0x8806/0x8807 first application byte is now the full
 *          PIB raw value (0x00..0x40) instead of the 6-bit masked level, so a
 *          valid raw of 0x40 is echoed intact; the second byte remains the
 *          legacy mapped value from the masked level. Custom general-diag TX
 *          fields (full raw / masked level / signed code) already matched. */
#define DIAG_BUILD_REVISION             (3U)

/* Per-request structure version accepted by every request handler. */
#define DIAG_REQ_VERSION                (1U)
/* Per-response structure version emitted by every response handler. */
#define DIAG_RSP_VERSION                (1U)

/* Stock firmware version this extension was compiled against. A compile-time
 * assertion in app_Znc_cmds.c checks this equals the local VERSION macro so
 * the deterministic build id below cannot silently drift. */
#define DIAG_FW_VERSION                 (0x00030323UL)

/* Capability bitmap. Advertise ONLY implemented, read-only-or-local
 * capabilities. No security, key, PDM-dump/restore or mutation bits exist. */
#define DIAG_CAP_BIT_GROUPS             (((uint64)1U) <<  0)
#define DIAG_CAP_BIT_NEIGHBOURS         (((uint64)1U) <<  1)
#define DIAG_CAP_BIT_ROUTES             (((uint64)1U) <<  2)
#define DIAG_CAP_BIT_TXPOWER            (((uint64)1U) <<  9)
/* Negotiated coordinator manufacturer-code override command (0x0D16/0x8D16).
 * Set ONLY because this firmware implements CUSTOMDIAG_vHandleManufCode; a
 * build without the handler must leave this bit clear so the host falls back
 * to zigbee.ErrUnsupported. */
#define DIAG_CAP_BIT_MANUFCODE          (((uint64)1U) << 10)
#define DIAG_CAP_BIT_DIAGNOSTICS        (((uint64)1U) << 14)

#define DIAG_CAP_BITMAP                 ( DIAG_CAP_BIT_GROUPS      \
                                        | DIAG_CAP_BIT_NEIGHBOURS  \
                                        | DIAG_CAP_BIT_ROUTES      \
                                        | DIAG_CAP_BIT_TXPOWER     \
                                        | DIAG_CAP_BIT_MANUFCODE   \
                                        | DIAG_CAP_BIT_DIAGNOSTICS )

/* 32-bit fold of the 64-bit capability bitmap. */
#define DIAG_CAP_FOLD32 \
    ((uint32)(DIAG_CAP_BITMAP & 0xFFFFFFFFUL) ^ \
     (uint32)((DIAG_CAP_BITMAP >> 32) & 0xFFFFFFFFUL))

/* Deterministic 32-bit firmware build id. Purely a compile-time constant, so
 * two clean builds of identical sources produce identical ids, but any change
 * to firmware version, protocol version, advertised capability set OR the
 * explicit build revision changes it. Contains no runtime, address or key
 * derived data. Layout: VERSION xor (proto_major<<24 | proto_minor<<16 |
 * build_revision) xor folded-capability-bitmap. */
#define DIAG_FW_BUILD_ID \
    ((uint32)(DIAG_FW_VERSION \
              ^ (((uint32)DIAG_PROTO_MAJOR << 24) \
                 | ((uint32)DIAG_PROTO_MINOR << 16) \
                 | ((uint32)DIAG_BUILD_REVISION & 0xFFFFU)) \
              ^ DIAG_CAP_FOLD32))

/****************************************************************************/
/***        Buffer sizing                                                 ***/
/****************************************************************************/

/* Maximum application payload (excluding the appended LQI byte) that any
 * custom response will emit. Advertised to hosts as the max unescaped
 * application payload. Chosen to sit comfortably below MAX_PACKET_SIZE (270)
 * once ZiGate framing/escaping overhead is accounted for. */
#define DIAG_TX_PAYLOAD_MAX             (200U)

/* vSL_WriteMessage() appends exactly one LQI byte past the payload. */
#define DIAG_TX_LQI_RESERVE             (1U)

#define DIAG_TX_BUFFER_SIZE             (DIAG_TX_PAYLOAD_MAX + DIAG_TX_LQI_RESERVE)

/****************************************************************************/
/***        Fixed request / response wire lengths                         ***/
/****************************************************************************/

/* Capability negotiation. */
#define DIAG_CAP_MAGIC_0                (0x5AU)  /* 'Z' */
#define DIAG_CAP_MAGIC_1                (0x47U)  /* 'G' */
#define DIAG_CAP_MAGIC_2                (0x48U)  /* 'H' */
#define DIAG_CAP_MAGIC_3                (0x58U)  /* 'X' */
/* magic[4] host_major[1] host_minor[1] nonce[4] */
#define DIAG_CAP_REQ_LEN                (10U)
/* magic[4] nonce[4] proto_major[1] proto_minor[1] build_id[4] cap[8] maxpl[2] */
#define DIAG_CAP_RSP_LEN                (24U)

/* Common paginated response prefix:
 *   version[1] status[1] flags[1] start_index[1] total[1] returned[1]
 *   next_index[1]
 * next_index is the physical table index at which the host should resume the
 * next request. It equals the index one past the last slot scanned; when the
 * scan reached the end of the table it equals total (capped at 0xFF), so the
 * host terminates when next_index >= total. This makes pagination unambiguous
 * across unused/holed table slots. */
#define DIAG_PAGE_PREFIX_LEN            (7U)

/* Neighbour table. */
#define DIAG_NEIGHBOUR_REQ_LEN          (4U)   /* version start max flags */
#define DIAG_NEIGHBOUR_RECORD_LEN       (23U)
#define DIAG_NEIGHBOUR_MAX_RECORDS      (8U)
#define DIAG_NEIGHBOUR_FLAG_INCLUDE_UNUSED  (0x01U)
#define DIAG_NEIGHBOUR_FLAG_MASK        (0x01U)  /* only bit0 is defined */

/* Route table. */
#define DIAG_ROUTE_REQ_LEN              (4U)   /* version start max flags */
#define DIAG_ROUTE_RECORD_LEN           (9U)
#define DIAG_ROUTE_MAX_RECORDS          (16U)
#define DIAG_ROUTE_FLAG_INCLUDE_INACTIVE    (0x01U)
#define DIAG_ROUTE_FLAG_MASK            (0x01U)  /* only bit0 is defined */

/* Group operation. */
#define DIAG_GROUP_OP_REQ_LEN           (5U)   /* version op endpoint group_id[2] */
#define DIAG_GROUP_OP_ADD               (0U)
#define DIAG_GROUP_OP_REMOVE            (1U)
#define DIAG_GROUP_OP_REMOVE_ALL        (2U)
/* version status op endpoint group_id[2] zps_status used total */
#define DIAG_GROUP_OP_RSP_LEN           (9U)
/* Operation-status domain for the response status byte (distinct from the
 * outer 0x8000 parse/dispatch status). */
#define DIAG_GROUP_OP_STATUS_OK         (0U)
#define DIAG_GROUP_OP_STATUS_ZPS_ERROR  (1U)

/* Group list. */
#define DIAG_GROUP_LIST_REQ_LEN         (4U)   /* version start max flags */
#define DIAG_GROUP_LIST_MAX_RECORDS     (5U)
#define DIAG_GROUP_LIST_MAX_ENDPOINTS   (16U)
#define DIAG_GROUP_LIST_FLAG_MASK       (0x00U) /* no flags defined; must be 0 */
/* Per-row fixed header: index[1] group_id[2] endpoint_count[1] */
#define DIAG_GROUP_LIST_ROW_HDR_LEN     (4U)
#define DIAG_GROUP_LIST_ROW_MAX_LEN     (DIAG_GROUP_LIST_ROW_HDR_LEN + DIAG_GROUP_LIST_MAX_ENDPOINTS)

/* General diagnostics response flags. */
#define DIAG_GENDIAG_FLAG_TCLK_UNAVAILABLE  (0x01U)

/* Manufacturer-code override (0x0D16/0x8D16). Request: version op code[2].
 * Response: version status effective[2] default[2]. All big-endian; the LQI
 * byte appended by vSL_WriteMessage() is not part of these lengths. */
#define DIAG_MANUF_CODE_REQ_LEN         (4U)
#define DIAG_MANUF_CODE_RSP_LEN         (6U)
#define DIAG_MANUF_OP_GET               (0U)
#define DIAG_MANUF_OP_SET               (1U)
#define DIAG_MANUF_OP_RESTORE           (2U)
#define DIAG_MANUF_STATUS_OK            (0U)
#define DIAG_MANUF_STATUS_INVALID       (1U)
#define DIAG_MANUF_STATUS_UNSUPPORTED_OP (2U)

/****************************************************************************/
/***        Sentinels                                                     ***/
/****************************************************************************/

#define DIAG_U8_NA                      (0xFFU)
#define DIAG_U16_NA                     (0xFFFFU)
#define DIAG_IEEE_NA                    (0xFFFFFFFFFFFFFFFFULL)

/****************************************************************************/
/***        Compile-time assertions (BA GCC 4.7.4 compatible)             ***/
/****************************************************************************/

#define DIAG_STATIC_ASSERT(cond, tag) \
    typedef char diag_static_assert_##tag[(cond) ? 1 : -1]

DIAG_STATIC_ASSERT(DIAG_CAP_RSP_LEN == 24U, cap_rsp_len);
DIAG_STATIC_ASSERT(DIAG_CAP_REQ_LEN == 10U, cap_req_len);
DIAG_STATIC_ASSERT(DIAG_PAGE_PREFIX_LEN == 7U, page_prefix_len);
/* Every full page must fit inside the advertised payload budget. */
DIAG_STATIC_ASSERT(
    DIAG_PAGE_PREFIX_LEN + (DIAG_NEIGHBOUR_MAX_RECORDS * DIAG_NEIGHBOUR_RECORD_LEN)
        <= DIAG_TX_PAYLOAD_MAX, neighbour_page_fits);
DIAG_STATIC_ASSERT(
    DIAG_PAGE_PREFIX_LEN + (DIAG_ROUTE_MAX_RECORDS * DIAG_ROUTE_RECORD_LEN)
        <= DIAG_TX_PAYLOAD_MAX, route_page_fits);
DIAG_STATIC_ASSERT(
    DIAG_PAGE_PREFIX_LEN + (DIAG_GROUP_LIST_MAX_RECORDS * DIAG_GROUP_LIST_ROW_MAX_LEN)
        <= DIAG_TX_PAYLOAD_MAX, group_list_page_fits);
DIAG_STATIC_ASSERT(DIAG_CAP_RSP_LEN <= DIAG_TX_PAYLOAD_MAX, cap_fits);
DIAG_STATIC_ASSERT(DIAG_GROUP_OP_RSP_LEN <= DIAG_TX_PAYLOAD_MAX, group_op_fits);
DIAG_STATIC_ASSERT(DIAG_MANUF_CODE_RSP_LEN <= DIAG_TX_PAYLOAD_MAX, manuf_rsp_fits);
/* The advertised endpoint-number space fits the group endpoint bitmap. */
DIAG_STATIC_ASSERT(DIAG_GROUP_LIST_MAX_ENDPOINTS <= 242U, group_ep_cap);

/****************************************************************************/
/***        Public handler entry points                                   ***/
/****************************************************************************/

/* Each handler validates its request with strict fixed-length bounds, emits
 * the stock 7-byte E_SL_MSG_STATUS (0x8000) frame first, then, on success,
 * emits its versioned response frame. pu8Rx points at the received UART
 * payload (au8LinkRxBuffer); u16Len is the received payload length. */
PUBLIC void CUSTOMDIAG_vHandleCapability(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void CUSTOMDIAG_vHandleGeneralDiag(uint16 u16Len);
PUBLIC void CUSTOMDIAG_vHandleNeighbours(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void CUSTOMDIAG_vHandleRoutes(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void CUSTOMDIAG_vHandleGroupOp(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void CUSTOMDIAG_vHandleGroupList(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void CUSTOMDIAG_vHandleManufCode(uint16 u16Len, const uint8 *pu8Rx);

#endif /* CUSTOM_DIAG_H_ */
