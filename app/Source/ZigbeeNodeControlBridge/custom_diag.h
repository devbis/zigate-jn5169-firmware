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
 *     - Local GP proxy commissioning     (0x0D17 / 0x8D17)
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
 * wire-compatible with the never-released 1.0 draft.
 *
 * 1.2 (rev4+) changes TX-power wire semantics: the stock 0x8806/0x8807 first
 * application byte and the custom general-diag (0x0D1F/0x8D1F) TX-power fields
 * now carry the canonical SIX-BIT code (GET & 0x3F) instead of the rev3 full
 * PIB byte with a phantom 0x40 tolerance bit, and SET rejects non-round-
 * trippable codes. Hosts must treat this as a wire-observable change. */
#define DIAG_PROTO_MAJOR                (1U)
#define DIAG_PROTO_MINOR                (2U)

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
 *          fields (full raw / masked level / signed code) already matched.
 *          (Superseded from rev6: see the rev6 note on the deliberate 0x8D1F
 *          vs 0x8806/0x8807 byte1 divergence.)
 *   rev 4: rev3 TX semantics CORRECTED per HIL + MiniMac disassembly. 0x40 is
 *          NOT round-trippable (SET sign-extends low 6 bits to 0; GET returns a
 *          sign-extended i8). Canonical semantics: SET accepts only exact non-
 *          clamping codes 0x00..0x0A and 0x20..0x3F and rejects 0x0B..0x1F and
 *          0x40+; the 0x8806/0x8807 byte0 is now GET & 0x3F (canonical six-bit
 *          code) with byte1 the legacy mapped level; general-diag TX fields are
 *          [six-bit code][six-bit code][signed six-bit code]. Also: the 0x0D00
 *          TCLK diagnostic feature (crypto-path --wrap interposition + internal
 *          security-state export) was REMOVED as security-sensitive; the three
 *          general-diag TCLK bytes are now always NA with TCLK_UNAVAILABLE set.
 *          Green Power coordinator RAM footprint reduced.
 *   rev 5: advertise Green Power commissioning only in builds that actually
 *          include CLD_GREENPOWER. This is an additive capability bit; the
 *          1.2 wire structures and command encodings are unchanged.
 *   rev 6: make the general-diagnostics TX level match the canonical MiniMac
 *          PIB code already returned by TX GET. The protocol 1.2 layout and
 *          command encodings are unchanged.
 *          NOTE - this makes the 0x8D1F and 0x8806/0x8807 TX byte1 fields
 *          DIVERGE, deliberately and permanently:
 *            0x8D1F  byte0/byte1 = canonical raw six-bit register code
 *                    (GET & 0x3F), byte2 = signed six-bit code.
 *            0x8806/0x8807  byte0 = same six-bit code, byte1 = LEGACY MAPPED
 *                    level from the threshold ladder in app_Znc_cmds.c
 *                    (<=31 -> 0, <=39 -> 32, <=51 -> 20, else 9).
 *          rev4's note that the general-diag fields "already matched" the
 *          legacy mapping describes rev3/rev4 and is NO LONGER true for
 *          byte1 from rev6 onwards. The divergence is intentional: 0x8D1F is
 *          the canonical register view for diagnostics, 0x8806/0x8807 keep the
 *          legacy ZiGate semantics deployed hosts already parse. Neither is
 *          changed for the sake of consistency with the other; hosts key the
 *          0x8D1F interpretation off build revision >= 6.
 *   rev 7: add the negotiated Green Power proxy commissioning command
 *          (0x0D17/0x8D17). Capability bit 1<<3 is unchanged in value but is
 *          now asserted only when this handler is compiled in, so hosts that
 *          see the bit can rely on the command instead of an unmanaged raw
 *          0x0530 APS broadcast (which the rev6 HIL rejected with
 *          ZPS_APL_APS_E_NO_ACK, 0xA6). Additive, capability-gated ABI: the
 *          1.2 wire structures and every pre-existing command encoding are
 *          unchanged.
 *   rev 8: add an explicit per-request transaction id to the Green Power
 *          proxy commissioning command only (0x0D17 request 3 -> 7 bytes,
 *          0x8D17 response 5 -> 9 bytes). The superseded first rev8 draft
 *          used a one-byte transaction id and 4/6-byte wire structures; the
 *          final ABI uses a big-endian uint32 transaction id. The rev7
 *          encoding carried no
 *          correlation field, so a late 0x8D17 from a host request that had
 *          already timed out was structurally indistinguishable from the
 *          answer to the NEXT request and could be consumed by it, reporting
 *          a stale window state as the current one. The firmware now echoes
 *          the request's transaction id in every response it emits for a
 *          structurally valid request, so the host can reject anything it did
 *          not ask for. Protocol stays 1.2: 0x0D17/0x8D17 were introduced in
 *          rev7 and are gated behind capability bit 1<<3, so no shipped host
 *          can be using the rev7 encoding without re-negotiating the build id
 *          first. Every other command encoding is unchanged.
 *   rev 9: restore the endpoint-1 raw-NCP transmit allowlist after physical
 *          HIL. No wire format, command encoding or capability bit changes;
 *          this revision exists only so a host can tell, from the build id,
 *          whether the image it is talking to can originate the affected
 *          clusters at all. The rev6 descriptor pruning treated endpoint 1's
 *          OutputClusters as pure ZCL advertisement and removed every cluster
 *          without a local client instance. HIL shows the ZPS APS layer also
 *          uses that same list as the allowlist for RAW, host-originated
 *          transmissions (serial 0x0530): a Basic 0x0000 read succeeds only
 *          because Basic survived the pruning, while a Power Configuration
 *          0x0001 read or configure-reporting is rejected LOCALLY by the
 *          JN5169 with APS status 0xA3 (ZPS_APL_APS_E_ILLEGAL_REQUEST) before
 *          anything reaches the air. In an NCP the output-cluster list is
 *          therefore a truthful statement of what the host+NCP pair can
 *          originate, not a claim about firmware-resident ZCL clients, so
 *          rev9 re-adds the clusters the hub legitimately originates
 *          (Power Configuration, Multistate Input, OTA, Thermostat UI
 *          configuration, Illuminance Level Sensing, Pressure, Occupancy,
 *          Electrical Measurement). This is descriptor-only: const/flash,
 *          no new ZCL instances, no RAM.
 *          rev9 additionally makes the endpoint-1 Time server (cluster 0x000A)
 *          READ-ONLY OVER ZIGBEE. Registering that server for network reads
 *          also exposed a network WRITE path, because ZclTime.h marks
 *          Time/TimeStatus E_ZCL_AF_WR and the ZCL core clamps the cluster's
 *          E_ZCL_SECURITY_APPLINK requirement down to E_ZCL_SECURITY_NETWORK
 *          in this build; any node holding only the network key could have
 *          rewritten the clock the host reads back over 0x0017. Remote ZCL
 *          Write Attributes to 0x000A are now refused with ZCL status 0x7e
 *          NOT_AUTHORIZED before the attribute is modified, in raw mode too.
 *          Host SET/GET (0x0016/0x0017) and the 1 Hz increment are unchanged;
 *          this touches no host wire encoding, no capability bit and no
 *          protocol field, so it did not warrant a rev10 - and rev9 was never
 *          flashed or published, so it was folded in place exactly as the
 *          superseded rev8 draft was. */
#define DIAG_BUILD_REVISION             (9U)

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
#define DIAG_CAP_BIT_GP_COMMISSIONING   (((uint64)1U) <<  3)
#define DIAG_CAP_BIT_TXPOWER            (((uint64)1U) <<  9)
/* Negotiated coordinator manufacturer-code override command (0x0D16/0x8D16).
 * Set ONLY because this firmware implements CUSTOMDIAG_vHandleManufCode; a
 * build without the handler must leave this bit clear so the host falls back
 * to zigbee.ErrUnsupported. */
#define DIAG_CAP_BIT_MANUFCODE          (((uint64)1U) << 10)
#define DIAG_CAP_BIT_DIAGNOSTICS        (((uint64)1U) << 14)

/* Green Power proxy commissioning (0x0D17/0x8D17) is built only when the GP
 * cluster itself is compiled in. DIAG_HAVE_GP_COMMISSIONING is the single
 * switch: it gates the handler prototype, the handler body in custom_diag.c,
 * the SerialLink dispatch case in app_Znc_cmds.c AND the advertised capability
 * bit, so the bit can never be advertised by a build that lacks the handler.
 * (rev5 gated only the bit on CLD_GREENPOWER while no handler existed at all;
 * hosts therefore fell back to a raw, unmanaged 0x0530 APS broadcast.) */
#ifdef CLD_GREENPOWER
#define DIAG_HAVE_GP_COMMISSIONING      (1)
#endif

#ifdef DIAG_HAVE_GP_COMMISSIONING
#define DIAG_CAP_GP_BITMAP              DIAG_CAP_BIT_GP_COMMISSIONING
#else
#define DIAG_CAP_GP_BITMAP              (((uint64)0U))
#endif

#define DIAG_CAP_BITMAP                 ( DIAG_CAP_BIT_GROUPS       \
                                        | DIAG_CAP_BIT_NEIGHBOURS   \
                                        | DIAG_CAP_BIT_ROUTES       \
                                        | DIAG_CAP_GP_BITMAP        \
                                        | DIAG_CAP_BIT_TXPOWER      \
                                        | DIAG_CAP_BIT_MANUFCODE    \
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

/* Shipped coordinator Node Descriptor manufacturer code. This MUST mirror
 * ZigbeeNodeControlBridgeCoordinator_GP_Proxy.zpscfg
 *   <NodeDescriptor ManufacturerCode="4423" .../>   (4423 == 0x1147)
 * which zps_gen.c bakes into the live descriptor at boot. It is the
 * RESTORE_DEFAULT target and the "default code" response field.
 *
 * NOTE: this is deliberately NOT ZCL_MANUFACTURER_CODE (0x1037) -- that is the
 * ZCL Basic-cluster attribute default, a different value that must not be used
 * to restore the ZDP Node Descriptor. The handler snapshots the LIVE descriptor
 * once before any SET can mutate it and prefers that runtime value as the
 * single source of truth; this constant is only the fallback used if the
 * descriptor is unavailable at the first call. */
#define DIAG_MANUF_CODE_SHIPPED_DEFAULT (0x1147U)

/* Green Power proxy commissioning window (0x0D17 / 0x8D17).
 *
 * Request  (exactly DIAG_GP_COMMISSION_REQ_LEN bytes, no optional fields):
 *   version[1]  transaction_id[4, big-endian]  action[1]  timeout_seconds[1]
 *     version         MUST be DIAG_REQ_VERSION.
 *     transaction_id  host-chosen correlation tag, any 32-bit value, encoded
 *                     big-endian like the 0x0D0F capability nonce. The
 *                     firmware never interprets it and echoes it verbatim in
 *                     the response emitted for this request. It is 32 bits
 *                     wide so a host counter cannot wrap between queued
 *                     requests and hand two in-flight transactions the same
 *                     tag.
 *     action          DIAG_GP_ACTION_DISABLE (0) or DIAG_GP_ACTION_ENABLE (1).
 *     timeout_seconds MUST be 0 for DISABLE and 1..255 for ENABLE. Any other
 *                     combination is rejected with the outer 0x8000
 *                     E_SL_MSG_STATUS_INCORRECT_PARAMETERS and NO 0x8D17.
 *
 * Response (exactly DIAG_GP_COMMISSION_RSP_LEN bytes):
 *   version[1]  transaction_id[4, big-endian]  status[1]  effective_mode[1]
 *   effective_timeout[1]  gp_status[1]
 *     transaction_id    verbatim echo of the request's transaction id. It is
 *                       echoed for EVERY response emitted for a structurally
 *                       valid request, success or Green Power failure, so the
 *                       host can discard a late response belonging to an
 *                       earlier, already timed-out transaction instead of
 *                       mistaking it for the answer to the current one.
 *     status            DIAG_GP_COMMISSION_STATUS_* (operation domain).
 *                       DIAG_GP_COMMISSION_STATUS_OK is emitted if and only if
 *                       gp_status is 0 (E_ZCL_SUCCESS), and
 *                       DIAG_GP_COMMISSION_STATUS_GP_ERROR if and only if
 *                       gp_status is non-zero.
 *     effective_mode    proxy commissioning state AFTER the operation
 *                       (0 = operating / closed, 1 = commissioning open).
 *     effective_timeout seconds the local commissioning window was programmed
 *                       with (0 when closed).
 *     gp_status         underlying teZCL_Status from the GP cluster call
 *                       (0 == E_ZCL_SUCCESS).
 *
 * The LQI byte appended by vSL_WriteMessage() is not part of these lengths. */
#define DIAG_GP_COMMISSION_REQ_LEN      (7U)
#define DIAG_GP_COMMISSION_RSP_LEN      (9U)
#define DIAG_GP_ACTION_DISABLE          (0U)
#define DIAG_GP_ACTION_ENABLE           (1U)
#define DIAG_GP_TIMEOUT_MIN             (1U)
#define DIAG_GP_TIMEOUT_MAX             (255U)
#define DIAG_GP_MODE_OPERATING          (0U)
#define DIAG_GP_MODE_COMMISSIONING      (1U)
#define DIAG_GP_COMMISSION_STATUS_OK        (0U)
#define DIAG_GP_COMMISSION_STATUS_GP_ERROR  (1U)

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
DIAG_STATIC_ASSERT(DIAG_GP_COMMISSION_RSP_LEN <= DIAG_TX_PAYLOAD_MAX, gp_rsp_fits);
/* The correlated rev8 encoding is fixed-width in both directions and carries a
 * 32-bit transaction id (1 + 4 + 1 + 1 request, 1 + 4 + 4 response). */
DIAG_STATIC_ASSERT(DIAG_GP_COMMISSION_REQ_LEN == 7U, gp_req_len);
DIAG_STATIC_ASSERT(DIAG_GP_COMMISSION_RSP_LEN == 9U, gp_rsp_len);
DIAG_STATIC_ASSERT(DIAG_GP_TIMEOUT_MAX <= 0xFFU, gp_timeout_fits_u8);
/* The advertised endpoint-number space fits the group endpoint bitmap. */
DIAG_STATIC_ASSERT(DIAG_GROUP_LIST_MAX_ENDPOINTS <= 242U, group_ep_cap);

/****************************************************************************/
/***        Public handler entry points                                   ***/
/****************************************************************************/

/* Each handler validates its request with strict fixed-length bounds, emits
 * the stock 8-byte E_SL_MSG_STATUS (0x8000) frame first, then, on success,
 * emits its versioned response frame. pu8Rx points at the received UART
 * payload (au8LinkRxBuffer); u16Len is the received payload length. */
PUBLIC void CUSTOMDIAG_vHandleCapability(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void CUSTOMDIAG_vHandleGeneralDiag(uint16 u16Len);
PUBLIC void CUSTOMDIAG_vHandleNeighbours(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void CUSTOMDIAG_vHandleRoutes(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void CUSTOMDIAG_vHandleGroupOp(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void CUSTOMDIAG_vHandleGroupList(uint16 u16Len, const uint8 *pu8Rx);
PUBLIC void CUSTOMDIAG_vHandleManufCode(uint16 u16Len, const uint8 *pu8Rx);
#ifdef DIAG_HAVE_GP_COMMISSIONING
PUBLIC void CUSTOMDIAG_vHandleGPCommission(uint16 u16Len, const uint8 *pu8Rx);
#endif

#endif /* CUSTOM_DIAG_H_ */
