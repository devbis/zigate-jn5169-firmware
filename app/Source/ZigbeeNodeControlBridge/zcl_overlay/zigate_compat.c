/****************************************************************************
 * zigate_compat.c — implementation for the ZiGate/v2395 app-level overlay.
 * See zigate_compat.h for rationale and provenance.
 ****************************************************************************/
#include <jendefs.h>
#include "zcl_options.h"
#include "zcl.h"
#include "pdum_gen.h"
#include "zigate_compat.h"

/* Standalone UTC counter that replaces OpenLumi's sControlBridge.sTimeServerCluster. */
PUBLIC tsCLD_Time sZigateTimeServerCluster;

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
