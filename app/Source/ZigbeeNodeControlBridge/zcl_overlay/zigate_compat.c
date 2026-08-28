/****************************************************************************
 * zigate_compat.c — implementation for the ZiGate/v2395 app-level overlay.
 * See zigate_compat.h for rationale and provenance.
 ****************************************************************************/
#include <jendefs.h>
#include "zcl_options.h"
#include "zcl.h"
#include "pdum_gen.h"
#include "zigate_compat.h"
#include <stddef.h>

/* Shared host/ZCL UTC storage. */
PUBLIC tsCLD_Time sZigateTimeServerCluster;

/*---------------------------------------------------------------------------*
 * Time cluster write authorization (host-owned / read-only over Zigbee).
 *
 * Static proof of the SDK contract this veto relies on
 * (Components/ZCIF/Source/zcl_WriteAttributesRequestHandle.c:284-300):
 *
 *   sZCL_CallBackEvent.eEventType = E_ZCL_CBET_CHECK_ATTRIBUTE_RANGE;
 *   ...eAttributeStatus = E_ZCL_SUCCESS;
 *   psZCL_EndPointDefinition->pCallBackFunctions(&sZCL_CallBackEvent);
 *   if (...eAttributeStatus != E_ZCL_CMDS_SUCCESS) {
 *       if (E_ZCL_ERR_ATTRIBUTES_ACCESS == ...eAttributeStatus)
 *            u8errorCode = E_ZCL_CMDS_NOT_AUTHORIZED;
 *       else u8errorCode = E_ZCL_CMDS_INVALID_VALUE;
 *   }
 *
 * and the mutation at :324-337 is gated on u8errorCode == E_ZCL_CMDS_SUCCESS,
 * so a non-zero code refuses the write BEFORE u16ZCL_WriteAttributeValueInto-
 * Structure() runs. For the undivided variant the range check runs on pass 0
 * and clears bNoErrors (declared once at :133, never reset), which gates the
 * pass-1 write via (bNoErrors || !bIsUndivided) -> the whole undivided record
 * set is refused. The three asserts below pin the enum values that make the
 * "!= SUCCESS" test fire and select the NOT_AUTHORIZED branch rather than the
 * INVALID_VALUE fallback; a future SDK renumbering breaks the build instead of
 * silently degrading the veto to a soft error.
 *---------------------------------------------------------------------------*/
typedef char tsZigateTimeVetoIsNotSuccess[
    ((int)E_ZCL_ERR_ATTRIBUTES_ACCESS != (int)E_ZCL_CMDS_SUCCESS) ? 1 : -1];
typedef char tsZigateTimeVetoSuccessIsZero[
    ((int)E_ZCL_CMDS_SUCCESS == 0) ? 1 : -1];
typedef char tsZigateTimeVetoMapsToNotAuthorized[
    ((int)E_ZCL_CMDS_NOT_AUTHORIZED == 0x7e) ? 1 : -1];

PUBLIC bool_t bZigate_VetoRemoteTimeWrite(tsZCL_CallBackEvent *psEvent)
{
    if (psEvent == NULL)
    {
        return FALSE;
    }

    /* Emitted only by the remote Write Attributes handler, so host SET
     * (E_SL_MSG_SET_TIMESERVER) and the 1 Hz increment are unaffected. */
    if (psEvent->eEventType != E_ZCL_CBET_CHECK_ATTRIBUTE_RANGE)
    {
        return FALSE;
    }

    if ((psEvent->psClusterInstance == NULL) ||
        (psEvent->psClusterInstance->psClusterDefinition == NULL))
    {
        return FALSE;
    }

    if (psEvent->psClusterInstance->psClusterDefinition->u16ClusterEnum !=
            GENERAL_CLUSTER_ID_TIME)
    {
        return FALSE;
    }

    /* Server side only: a Time *client* instance has no writable server
     * attributes to protect, and vetoing there would be a behaviour change. */
    if (psEvent->psClusterInstance->bIsServer == FALSE)
    {
        return FALSE;
    }

    /* Refuse every attribute of the cluster, not just Time/TimeStatus: the SDK
     * only reaches this callback after its own E_ZCL_AF_WR and direction checks
     * pass, so anything arriving here is a genuine remote write attempt. */
    psEvent->uMessage.sIndividualAttributeResponse.eAttributeStatus =
        (teZCL_CommandStatus)E_ZCL_ERR_ATTRIBUTES_ACCESS;

    return TRUE;
}

#ifdef ZIGATE_CONTROL_BRIDGE_OVERLAY

/*
 * The stock ControlBridge registration uses sizeof(sClusterInstance), so the
 * three app-only instances appended in control_bridge.h are registered by the
 * stock routine.  Keep them last and contiguous: this is the invariant that
 * makes the small header extension sufficient without forking control_bridge.c.
 */
typedef char tsZigateOverlayTimeIsFirst[
    (offsetof(tsZLO_ControlBridgeDeviceClusterInstances, sWindowCoveringClient) ==
     offsetof(tsZLO_ControlBridgeDeviceClusterInstances, sTimeServer) +
     sizeof(tsZCL_ClusterInstance)) ? 1 : -1];
typedef char tsZigateOverlayIASIsSecond[
    (offsetof(tsZLO_ControlBridgeDeviceClusterInstances, sIASWarningDeviceClient) ==
     offsetof(tsZLO_ControlBridgeDeviceClusterInstances, sWindowCoveringClient) +
     sizeof(tsZCL_ClusterInstance)) ? 1 : -1];
typedef char tsZigateOverlayIsTail[
    (offsetof(tsZLO_ControlBridgeDeviceClusterInstances, sIASWarningDeviceClient) +
     sizeof(tsZCL_ClusterInstance) ==
     sizeof(tsZLO_ControlBridgeDeviceClusterInstances)) ? 1 : -1];

PUBLIC teZCL_Status eZigate_CreateControlBridgeOverlay(
    tsZLO_ControlBridgeDevice *psDeviceInfo)
{
    teZCL_Status eStatus;

    eStatus = eCLD_TimeCreateTime(
        &psDeviceInfo->sClusterInstance.sTimeServer,
        TRUE,
        &sCLD_Time,
        &sZigateTimeServerCluster,
        &au8TimeClusterAttributeControlBits[0]);
    if (eStatus != E_ZCL_SUCCESS)
    {
        return eStatus;
    }

    eStatus = eCLD_WindowCoveringCreateWindowCovering(
        &psDeviceInfo->sClusterInstance.sWindowCoveringClient,
        FALSE,
        &sCLD_WindowCoveringClient,
        &psDeviceInfo->sWindowCoveringClientCluster,
        &au8WindowCoveringClientAttributeControlBits[0],
        &psDeviceInfo->sWindowCoveringClientCustomDataStructure);
    if (eStatus != E_ZCL_SUCCESS)
    {
        return eStatus;
    }

    return eCLD_IASWDCreateIASWD(
        &psDeviceInfo->sClusterInstance.sIASWarningDeviceClient,
        FALSE,
        &sCLD_IASWD,
        &psDeviceInfo->sIASWarningDeviceClientCluster,
        &au8IASWDAttributeControlBits[0],
        &psDeviceInfo->sIASWarningDeviceClientCustomDataStructure);
}

#endif /* ZIGATE_CONTROL_BRIDGE_OVERLAY */

/* APDU-pool usage diagnostics over the generated pool handle (apduZDP). */
PUBLIC uint8 u8GetApduUse(void)
{
    return (uint8)PDUM_u16APduGetCrtUse(apduZDP);
}

PUBLIC uint8 u8GetMaxApdu(void)
{
    return (uint8)PDUM_u16APduGetMaxUse(apduZDP);
}

#ifdef CLD_WINDOWCOVERING

PUBLIC teZCL_Status eCLD_WindowCoveringCommandOpenCloseStopRequestSend(
    uint8          u8SourceEndPointId,
    uint8          u8DestinationEndPointId,
    tsZCL_Address *psDestinationAddress,
    uint8         *pu8TransactionSequenceNumber,
    uint8          u8CommandId)
{
    return eCLD_WindowCoveringCommandSend(
        u8SourceEndPointId,
        u8DestinationEndPointId,
        psDestinationAddress,
        pu8TransactionSequenceNumber,
        (teCLD_WindowCovering_Command)u8CommandId);
}

PUBLIC teZCL_Status eCLD_WindowCoveringCommandGotoValueRequestSend(
    uint8          u8SourceEndPointId,
    uint8          u8DestinationEndPointId,
    tsZCL_Address *psDestinationAddress,
    uint8         *pu8TransactionSequenceNumber,
    uint8          u8CommandId,
    tsCLD_WindowCovering_GoToValueRequestPayload *psPayload)
{
    if (u8CommandId == (uint8)E_CLD_WC_CMD_GO_TO_TILT_VALUE)
    {
        tsCLD_WindowCovering_GoToTiltValuePayload sTilt;
        sTilt.u16TiltValue = psPayload->u16Value;
        return eCLD_WindowCoveringCommandGoToTiltValueSend(
            u8SourceEndPointId, u8DestinationEndPointId,
            psDestinationAddress, pu8TransactionSequenceNumber, &sTilt);
    }
    else
    {
        tsCLD_WindowCovering_GoToLiftValuePayload sLift;
        sLift.u16LiftValue = psPayload->u16Value;
        return eCLD_WindowCoveringCommandGoToLiftValueSend(
            u8SourceEndPointId, u8DestinationEndPointId,
            psDestinationAddress, pu8TransactionSequenceNumber, &sLift);
    }
}

PUBLIC teZCL_Status eCLD_WindowCoveringCommandGotoPercentageRequestSend(
    uint8          u8SourceEndPointId,
    uint8          u8DestinationEndPointId,
    tsZCL_Address *psDestinationAddress,
    uint8         *pu8TransactionSequenceNumber,
    uint8          u8CommandId,
    tsCLD_WindowCovering_GoToPercentageRequestPayload *psPayload)
{
    if (u8CommandId == (uint8)E_CLD_WC_CMD_GO_TO_TILT_PERCENTAGE)
    {
        tsCLD_WindowCovering_GoToTiltPercentagePayload sTilt;
        sTilt.u8TiltPercentage = psPayload->u8Percentage;
        return eCLD_WindowCoveringCommandGoToTiltPercentageSend(
            u8SourceEndPointId, u8DestinationEndPointId,
            psDestinationAddress, pu8TransactionSequenceNumber, &sTilt);
    }
    else
    {
        tsCLD_WindowCovering_GoToLiftPercentagePayload sLift;
        sLift.u8LiftPercentage = psPayload->u8Percentage;
        return eCLD_WindowCoveringCommandGoToLiftPercentageSend(
            u8SourceEndPointId, u8DestinationEndPointId,
            psDestinationAddress, pu8TransactionSequenceNumber, &sLift);
    }
}

#endif /* CLD_WINDOWCOVERING */
