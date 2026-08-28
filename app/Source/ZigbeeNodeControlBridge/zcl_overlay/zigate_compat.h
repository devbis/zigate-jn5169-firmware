/****************************************************************************
 * zigate_compat.h — thin app-level overlay reconciling the OpenLumi ZiGate
 * v3.23 ControlBridge application with the stock JN-SW-4170 v2395 ZCL/device
 * layer (LEGACY=1).
 *
 * The OpenLumi application was written against a bundled JN-SW-4170 *1840* SDK
 * that OpenLumi had modified at the ZCL device/cluster layer. Stock v2395
 * renames or removes several of those symbols. Rather than dragging the
 * OpenLumi-1840 ZCL forward (which would ABI-clash with the v2395 prebuilt
 * libZPSAPL/libZCL), this overlay supplies faithful, self-contained
 * compatibility definitions compiled into the *application*.
 *
 * Provenance / evidence for each mapping is documented inline and in
 * docs/MIGRATION_STATUS.md.
 ****************************************************************************/
#ifndef ZIGATE_COMPAT_H
#define ZIGATE_COMPAT_H

#include <jendefs.h>
#include "zcl_options.h"
#include "zps_apl_aps.h" /* base ZPS_teAplApsdeAddressMode: BOUND/GROUP/SHORT/IEEE */

/*---------------------------------------------------------------------------*
 * ZiGate host-protocol extended APSDE address modes.
 *
 * Stock v2395 ZPS_teAplApsdeAddressMode (zps_apl_aps.h) defines only:
 *   ZPS_E_ADDR_MODE_BOUND=0, _GROUP=1, _SHORT=2, _IEEE=3.
 * The ZiGate host wire byte (au8LinkRxBuffer[0], assigned to eDstAddrMode)
 * also carries a broadcast mode and three "no-ack" unicast variants. The
 * numeric values below are the canonical ZiGate protocol address-mode codes
 * (identical to zigpy-zigate's ADDRESS_MODE enum, which the ZiGate/OpenLumi
 * host protocol implements). These EXTEND, and never redefine, the SDK enum.
 *---------------------------------------------------------------------------*/
enum {
    ZPS_E_ADDR_MODE_BROADCAST    = 0x04,
    ZPS_E_ADDR_MODE_BOUND_NO_ACK = 0x06,
    ZPS_E_ADDR_MODE_SHORT_NO_ACK = 0x07,
    ZPS_E_ADDR_MODE_IEEE_NO_ACK  = 0x08
};

/*---------------------------------------------------------------------------*
 * Window Covering command/payload compatibility.
 *
 * v2395 renames E_CLD_WINDOWCOVERING_CMD_* -> E_CLD_WC_CMD_* and splits the
 * generic Goto{Value,Percentage} payload into distinct Lift/Tilt payloads and
 * per-axis send functions. The app enables WINDOW_COVERING_CLIENT and the four
 * CLD_WC_CMD_GO_TO_* options in zcl_options.h so the v2395 client API is
 * declared/compiled; the wrappers below re-express the OpenLumi call shape
 * onto that API, dispatching Lift vs Tilt from the command id.
 *---------------------------------------------------------------------------*/
#ifdef CLD_WINDOWCOVERING
#include "WindowCovering.h"

#define E_CLD_WINDOWCOVERING_CMD_UP_OPEN               E_CLD_WC_CMD_UP_OPEN
#define E_CLD_WINDOWCOVERING_CMD_DOWN_CLOSE            E_CLD_WC_CMD_DOWN_CLOSE
#define E_CLD_WINDOWCOVERING_CMD_STOP                  E_CLD_WC_CMD_STOP
#define E_CLD_WINDOWCOVERING_CMD_GO_TO_LIFT_VALUE      E_CLD_WC_CMD_GO_TO_LIFT_VALUE
#define E_CLD_WINDOWCOVERING_CMD_GO_TO_TILT_VALUE      E_CLD_WC_CMD_GO_TO_TILT_VALUE
#define E_CLD_WINDOWCOVERING_CMD_GO_TO_LIFT_PERCENTAGE E_CLD_WC_CMD_GO_TO_LIFT_PERCENTAGE
#define E_CLD_WINDOWCOVERING_CMD_GO_TO_TILT_PERCENTAGE E_CLD_WC_CMD_GO_TO_TILT_PERCENTAGE

/* OpenLumi carried the raw host value in a single payload; the wrappers map it
 * onto the v2395 axis-specific payloads. */
typedef struct { uint16 u16Value; }    tsCLD_WindowCovering_GoToValueRequestPayload;
typedef struct { uint8  u8Percentage; } tsCLD_WindowCovering_GoToPercentageRequestPayload;

PUBLIC teZCL_Status eCLD_WindowCoveringCommandOpenCloseStopRequestSend(
    uint8          u8SourceEndPointId,
    uint8          u8DestinationEndPointId,
    tsZCL_Address *psDestinationAddress,
    uint8         *pu8TransactionSequenceNumber,
    uint8          u8CommandId);

PUBLIC teZCL_Status eCLD_WindowCoveringCommandGotoValueRequestSend(
    uint8          u8SourceEndPointId,
    uint8          u8DestinationEndPointId,
    tsZCL_Address *psDestinationAddress,
    uint8         *pu8TransactionSequenceNumber,
    uint8          u8CommandId,
    tsCLD_WindowCovering_GoToValueRequestPayload *psPayload);

PUBLIC teZCL_Status eCLD_WindowCoveringCommandGotoPercentageRequestSend(
    uint8          u8SourceEndPointId,
    uint8          u8DestinationEndPointId,
    tsZCL_Address *psDestinationAddress,
    uint8         *pu8TransactionSequenceNumber,
    uint8          u8CommandId,
    tsCLD_WindowCovering_GoToPercentageRequestPayload *psPayload);
#endif /* CLD_WINDOWCOVERING */

/*---------------------------------------------------------------------------*
 * Time server storage compatibility.
 *
 * OpenLumi kept a free-running UTC counter inside its modified ControlBridge.
 * The app overlay registers this same storage as the endpoint-1 Time server,
 * so host GET/SET, the 1 Hz increment, and network Time reads share one value.
 *
 * OWNERSHIP MODEL: the value is HOST-OWNED and READ-ONLY OVER ZIGBEE.
 *   writers  : E_SL_MSG_SET_TIMESERVER (0x0016, UART host) and the 1 Hz
 *              increment in app_start.c. Both write the struct directly and
 *              never traverse the ZCL Write Attributes path.
 *   readers  : E_SL_MSG_GET_TIMESERVER (0x0017, UART host) and remote ZCL
 *              Read Attributes on endpoint 1 / cluster 0x000A.
 *   rejected : remote ZCL Write Attributes on cluster 0x000A -> ZCL status
 *              0x7e NOT_AUTHORIZED, refused BEFORE the attribute is mutated.
 *
 * Rationale: ZCL_Time declares E_ZCL_SECURITY_APPLINK, but zcl.c initialises
 * psZCL_Common->eSecuritySupported to E_ZCL_SECURITY_NETWORK and the app never
 * calls eZCL_SetSupportedSecurity(), so zcl_event.c clamps the cluster's
 * requirement down to NETWORK. Without the veto below, ZclTime.h's
 * (E_ZCL_AF_RD|E_ZCL_AF_WR) flags on Time/TimeStatus would let ANY node holding
 * only the network key rewrite the clock the host reads back over 0x0017.
 *---------------------------------------------------------------------------*/
#include "ZclTime.h"
extern tsCLD_Time sZigateTimeServerCluster;

/* Reject network-originated writes to the Time cluster.
 *
 * MUST be invoked unconditionally at the top of the endpoint callback, ahead of
 * any RAW_MODE_ON host-forwarding early-return, otherwise raw mode silently
 * bypasses authorization. Returns TRUE when a write was vetoed.
 *
 * E_ZCL_CBET_CHECK_ATTRIBUTE_RANGE is emitted from exactly one site in the SDK
 * (zcl_WriteAttributesRequestHandle.c), i.e. only for remote Write Attributes
 * requests, so this can never intercept a host SET or the 1 Hz increment. */
PUBLIC bool_t bZigate_VetoRemoteTimeWrite(tsZCL_CallBackEvent *psEvent);

#ifdef ZIGATE_CONTROL_BRIDGE_OVERLAY
#include "control_bridge.h"
PUBLIC teZCL_Status eZigate_CreateControlBridgeOverlay(
    tsZLO_ControlBridgeDevice *psDeviceInfo);
#endif

/*---------------------------------------------------------------------------*
 * General cluster-id headers the OpenLumi handlers reference but the app did
 * not include directly. MultistateInputBasic is a *stock* v2395 cluster; its
 * id (0x0012) is provided by this header unconditionally.
 *---------------------------------------------------------------------------*/
#include "MultistateInputBasic.h"

/*---------------------------------------------------------------------------*
 * APDU-pool usage diagnostics.
 *
 * The prototypes live in zigate_apdu_diag.h (jendefs.h only, no other
 * dependencies) so host-diagnostic translation units can declare them without
 * pulling in the ZCL cluster world this header exposes. Declared there ONCE;
 * do not restate them here.
 *---------------------------------------------------------------------------*/
#include "zigate_apdu_diag.h"

/*---------------------------------------------------------------------------*
 * OpenLumi private cluster extensions that depend on OpenLumi's *modified* SDK
 * ZCL and are NOT present in stock v2395:
 *   - OnOff private "Lora tap" report (command 0xFD, uMessage.u8LoraTapData),
 *   - IKEA remote Scenes manufacturer-specific commands
 *     (E_CLD_SCENES_CMD_IKEA_REMOTE_*, uMessage.psIkeaRemoteSceneCustomPayload).
 * These are gated OFF for the compiling baseline: reproducing them requires
 * forward-porting the corresponding OpenLumi ZCL cluster modifications, which
 * would ABI-clash with the v2395 prebuilt libZCL. Tracked as deferred quirks in
 * docs/MIGRATION_STATUS.md. Define the macro below (and supply the matching ZCL
 * overlay) to re-enable them later.
 *---------------------------------------------------------------------------*/
/* #define ZIGATE_ENABLE_OPENLUMI_PRIVATE_QUIRKS */

#endif /* ZIGATE_COMPAT_H */
