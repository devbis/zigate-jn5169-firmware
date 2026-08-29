/****************************************************************************
 *
 * This software is owned by NXP B.V. and/or its supplier and is protected
 * under applicable copyright laws. All rights are reserved. We grant You,
 * and any third parties, a license to use this software solely and
 * exclusively on NXP products [NXP Microcontrollers such as JN5168, JN5179].
 * You, and any third parties must reproduce the copyright and warranty notice
 * and any other legend of ownership on each copy or partial copy of the
 * software.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Copyright NXP B.V. 2016. All rights reserved
 ****************************************************************************/


/****************************************************************************/
/***        Include files                                                 ***/
/****************************************************************************/

#include <jendefs.h>
#include "SerialLink.h"
#include "app_common.h"
#include "AppHardwareApi.h"
#include "PDM.h"
#include "dbg.h"
#include "app_ahi_commands.h"

/****************************************************************************/
/***        Macro Definitions                                             ***/
/****************************************************************************/

#define APP_TX_POWER_RECORD_MAGIC_0       (0x54U) /* 'T' */
#define APP_TX_POWER_RECORD_MAGIC_1       (0x58U) /* 'X' */
#define APP_TX_POWER_RECORD_VERSION       (1U)
#define APP_TX_POWER_RECORD_CRC_SEED      (0xFFU)
#define APP_TX_POWER_RECORD_CRC_POLY      (0x07U)

/****************************************************************************/
/***        Type Definitions                                              ***/
/****************************************************************************/

typedef struct
{
    uint8 u8Magic0;
    uint8 u8Magic1;
    uint8 u8Version;
    uint8 u8TxPower;
    uint8 u8Check;
} tsAPP_TxPowerRecord;

/****************************************************************************/
/***        Local Function Prototypes                                     ***/
/****************************************************************************/
PRIVATE void vAPP_DIOSetDirection(uint16 u16PacketLength, uint8 *pu8LinkRxBuffer, eAHI_Status *peAHIStatus);
PRIVATE void vAPP_DIOSetOutput(uint16 u16PacketLength, uint8 *pu8LinkRxBuffer, eAHI_Status *peAHIStatus);
PRIVATE void vAPP_DIOSetReadInput(uint16 u16PacketLength, uint8 *pu8LinkRxBuffer, eAHI_Status *peAHIStatus);
PRIVATE void vAPP_AHISetTxPower(uint16 u16PacketLength, uint8 *pu8LinkRxBuffer, eAHI_Status *peAHIStatus);
PRIVATE void vAPP_AHIGetTxPower(uint32 *u32TxPower, eAHI_Status *peAHIStatus);
PRIVATE bool_t bAPP_AHIIsValidTxPower(uint8 u8TxPower);
PRIVATE uint8 u8APP_AHITxPowerRecordCheck(const tsAPP_TxPowerRecord *psRecord);
PRIVATE bool_t bAPP_AHILoadStoredTxPower(uint8 *pu8TxPower);
PRIVATE bool_t bAPP_AHISaveTxPower(uint8 u8TxPower);
/****************************************************************************/
/***        Exported Variables                                            ***/
/****************************************************************************/

/****************************************************************************/
/***        Local Variables                                               ***/
/****************************************************************************/

PRIVATE bool_t bAPP_AHIStoredTxPowerLoaded;
PRIVATE bool_t bAPP_AHIStoredTxPowerValid;
PRIVATE uint8 u8APP_AHIStoredTxPower;

/****************************************************************************/
/***        Exported Functions                                            ***/
/****************************************************************************/
/****************************************************************************
 *
 * NAME: APP_vCMDHandleAHICommand
 *
 * DESCRIPTION:
 * The main function that figures out which AHI command is to be executed
 * and passes the data to the correct function.
 *
 *
 ****************************************************************************/
PUBLIC uint32 APP_vCMDHandleAHICommand(uint16 u16PacketType,
                                     uint16 u16PacketLength,
                                     uint8 *pu8LinkRxBuffer,
                                     uint8 *peAHIStatus)
{
	eAHI_Status eAHI_Status;
	uint32 u32AHI_response = 0;

    switch (u16PacketType)
    {
        case E_SL_MSG_AHI_DIO_SET_DIRECTION:
        {
            vAPP_DIOSetDirection(u16PacketLength, pu8LinkRxBuffer, &eAHI_Status);
            break;
        }
        case E_SL_MSG_AHI_DIO_SET_OUTPUT:
        {
            vAPP_DIOSetOutput(u16PacketLength, pu8LinkRxBuffer, &eAHI_Status);
            break;
        }
        case E_SL_MSG_AHI_DIO_READ_INPUT:
        {
            vAPP_DIOSetReadInput(u16PacketLength, pu8LinkRxBuffer, &eAHI_Status);
            break;
        }

        case E_SL_MSG_AHI_SET_TX_POWER:
        	vAPP_AHISetTxPower(u16PacketLength, pu8LinkRxBuffer, &eAHI_Status);
			break;

        case E_SL_MSG_AHI_GET_TX_POWER:
        {
            vAPP_AHIGetTxPower(&u32AHI_response, &eAHI_Status);
            break;
        }

        default:
        	eAHI_Status = E_AHI_COMMAND_UNRECOGNISED;
            break;
    }

    *peAHIStatus = eAHI_Status;
    return u32AHI_response;
}

/****************************************************************************
 *
 * NAME: APP_vAHIApplyPersistedTxPower
 *
 * DESCRIPTION:
 * Reapplies the application-owned TX-power PIB on every NWK_STARTED event.
 * BDB has processed that event before forwarding it to the application, so
 * the associated MLME reset/default-PIB work is complete. The validated PDM
 * record is cached, so later events neither reread nor write PDM.
 *
 ****************************************************************************/
PUBLIC void APP_vAHIApplyPersistedTxPower(void)
{
    uint8 u8TxPower;
    uint32 u32OldTxPower;
    uint32 u32ReadBack;

    if (!bAPP_AHILoadStoredTxPower(&u8TxPower) ||
        eAppApiPlmeGet(PHY_PIB_ATTR_TX_POWER, &u32OldTxPower) !=
            PHY_ENUM_SUCCESS)
    {
        return;
    }

    if (eAppApiPlmeSet(PHY_PIB_ATTR_TX_POWER, u8TxPower) == PHY_ENUM_SUCCESS &&
        eAppApiPlmeGet(PHY_PIB_ATTR_TX_POWER, &u32ReadBack) ==
            PHY_ENUM_SUCCESS &&
        (uint8)(u32ReadBack & PHY_PIB_TX_POWER_MASK) == u8TxPower)
    {
        DBG_vPrintf(TRUE, "AHI: applied persisted TX power 0x%02x",
                    u8TxPower);
    }
    else
    {
        (void)eAppApiPlmeSet(PHY_PIB_ATTR_TX_POWER, u32OldTxPower);
    }
}

/****************************************************************************/
/***        Local Functions                                               ***/
/****************************************************************************/

PRIVATE bool_t bAPP_AHIIsValidTxPower(uint8 u8TxPower)
{
    return (u8TxPower <= 0x0A ||
            (u8TxPower >= 0x20 && u8TxPower <= 0x3F));
}

PRIVATE uint8 u8APP_AHITxPowerRecordCheck(const tsAPP_TxPowerRecord *psRecord)
{
    const uint8 au8Data[] = {
        psRecord->u8Magic0,
        psRecord->u8Magic1,
        psRecord->u8Version,
        psRecord->u8TxPower
    };
    uint8 u8Check = APP_TX_POWER_RECORD_CRC_SEED;
    uint8 u8Byte;
    uint8 u8Bit;

    for (u8Byte = 0; u8Byte < (uint8)sizeof(au8Data); u8Byte++)
    {
        u8Check ^= au8Data[u8Byte];
        for (u8Bit = 0; u8Bit < 8; u8Bit++)
        {
            u8Check = (uint8)((u8Check & 0x80U) ?
                      ((u8Check << 1) ^ APP_TX_POWER_RECORD_CRC_POLY) :
                      (u8Check << 1));
        }
    }

    return u8Check;
}

PRIVATE bool_t bAPP_AHILoadStoredTxPower(uint8 *pu8TxPower)
{
    tsAPP_TxPowerRecord sRecord;
    uint16 u16BytesRead = 0;

    if (!bAPP_AHIStoredTxPowerLoaded)
    {
        bAPP_AHIStoredTxPowerLoaded = TRUE;
        bAPP_AHIStoredTxPowerValid =
            (PDM_eReadDataFromRecord(PDM_ID_APP_TX_POWER,
                                     &sRecord,
                                     sizeof(sRecord),
                                     &u16BytesRead) == PDM_E_STATUS_OK &&
             u16BytesRead == sizeof(sRecord) &&
             sRecord.u8Magic0 == APP_TX_POWER_RECORD_MAGIC_0 &&
             sRecord.u8Magic1 == APP_TX_POWER_RECORD_MAGIC_1 &&
             sRecord.u8Version == APP_TX_POWER_RECORD_VERSION &&
             sRecord.u8Check == u8APP_AHITxPowerRecordCheck(&sRecord) &&
             bAPP_AHIIsValidTxPower(sRecord.u8TxPower));

        if (bAPP_AHIStoredTxPowerValid)
        {
            u8APP_AHIStoredTxPower = sRecord.u8TxPower;
        }
    }

    if (bAPP_AHIStoredTxPowerValid)
    {
        *pu8TxPower = u8APP_AHIStoredTxPower;
        return TRUE;
    }

    return FALSE;
}

PRIVATE bool_t bAPP_AHISaveTxPower(uint8 u8TxPower)
{
    tsAPP_TxPowerRecord sRecord;
    uint8 u8StoredTxPower;

    if (bAPP_AHILoadStoredTxPower(&u8StoredTxPower) &&
        u8StoredTxPower == u8TxPower)
    {
        return TRUE;
    }

    sRecord.u8Magic0 = APP_TX_POWER_RECORD_MAGIC_0;
    sRecord.u8Magic1 = APP_TX_POWER_RECORD_MAGIC_1;
    sRecord.u8Version = APP_TX_POWER_RECORD_VERSION;
    sRecord.u8TxPower = u8TxPower;
    sRecord.u8Check = u8APP_AHITxPowerRecordCheck(&sRecord);

    if (PDM_eSaveRecordData(PDM_ID_APP_TX_POWER,
                            &sRecord,
                            sizeof(sRecord)) == PDM_E_STATUS_OK)
    {
        bAPP_AHIStoredTxPowerValid = TRUE;
        u8APP_AHIStoredTxPower = u8TxPower;
        return TRUE;
    }

    return FALSE;
}

/****************************************************************************
 *
 * NAME: vAPP_DIOSetDirection
 *
 * DESCRIPTION:
 * Configures the DIO's to be inputs/outputs depending on the DIO masks passed.
 *
 * Params:
 * @pu8LinkRxBuffer: pointer to the DIO Set Direction message ready to be parsed.
 *
 *
 ****************************************************************************/
PRIVATE void vAPP_DIOSetDirection(uint16 u16PacketLength, uint8 *pu8LinkRxBuffer, eAHI_Status *peAHIStatus)
{
    uint32 u32DioInputPinMask;
    uint32 u32DioOutputPinMask;
    uint32 u32BytesRead = 0;

    *peAHIStatus = E_AHI_PARSE_ERROR;

    DBG_vPrintf(TRUE, "AHI: %s", __FUNCTION__);

    if (8 == u16PacketLength)
    {
        /* Parse the information out */
        u32DioInputPinMask = ZNC_RTN_U32( pu8LinkRxBuffer, u32BytesRead );
        u32BytesRead += sizeof(u32DioInputPinMask);
        u32DioOutputPinMask = ZNC_RTN_U32( pu8LinkRxBuffer, u32BytesRead );

        /* Configure the IPN */
        vAHI_DioSetOutput(u32DioInputPinMask, u32DioOutputPinMask);

        *peAHIStatus = E_AHI_SUCCESS;
    }
}

/****************************************************************************
 *
 * NAME: vAPP_DIOSetOutput
 *
 * DESCRIPTION:
 * Sets the DIOs to on or off depending on the DIO mask passed.
 *
 * Params:
 * @pu8LinkRxBuffer: pointer to the IPN message ready to be parsed.
 *
 *
 ****************************************************************************/
PRIVATE void vAPP_DIOSetOutput(uint16 u16PacketLength, uint8 *pu8LinkRxBuffer, eAHI_Status *peAHIStatus)
{
    uint32 u32DioOnPinMask;
    uint32 u32DioOffPinMask;
    uint32 u32BytesRead = 0;

    *peAHIStatus = E_AHI_PARSE_ERROR;

    DBG_vPrintf(TRUE, "AHI: %s", __FUNCTION__);

    if (8 == u16PacketLength)
    {
        /* Parse the information out */
        u32DioOnPinMask = ZNC_RTN_U32( pu8LinkRxBuffer, u32BytesRead );
        u32BytesRead += sizeof(u32DioOnPinMask);
        u32DioOffPinMask = ZNC_RTN_U32( pu8LinkRxBuffer, u32BytesRead );

        /* Configure the IPN */
        vAHI_DioSetDirection(u32DioOnPinMask, u32DioOffPinMask);

        *peAHIStatus = E_AHI_SUCCESS;
    }
}

/****************************************************************************
 *
 * NAME: vAPP_DIOSetReadInput
 *
 * DESCRIPTION:
 * Command to read the input pins on the DIO.
 *
 * Params:
 * @pu8LinkRxBuffer: pointer to the IPN message ready to be parsed.
 *
 *
 ****************************************************************************/
PRIVATE void vAPP_DIOSetReadInput(uint16 u16PacketLength, uint8 *pu8LinkRxBuffer, eAHI_Status *peAHIStatus)
{
    uint32 u32DioReadInput;
    *peAHIStatus = E_AHI_PARSE_ERROR;

    DBG_vPrintf(TRUE, "AHI: %s", __FUNCTION__);

    if (0 == u16PacketLength)
    {
        /* Read the input state */
        u32DioReadInput = u32AHI_DioReadInput();

        /* Need to figure out how to send this data back as the success needs to be sent first
         * if I send the read input data back now, the status message will return after this
         */
        *peAHIStatus = E_AHI_SUCCESS;
    }
}

/****************************************************************************
 *
 * NAME: vAPP_AHISetTxPower
 *
 * DESCRIPTION:
 * Command to set the Tx Power
 *
 * Params:
 * @pu8LinkRxBuffer: pointer to the message ready to be parsed.
 *
 *
 ****************************************************************************/
PRIVATE void vAPP_AHISetTxPower(uint16 u16PacketLength, uint8 *pu8LinkRxBuffer, eAHI_Status *peAHIStatus)
{
    uint8 u8TxPower;
    uint32 u32OldTxPower;
    uint32 u32ReadBack;
    uint32 u32BytesRead = 0;
    *peAHIStatus = E_AHI_PARSE_ERROR;

    DBG_vPrintf(TRUE, "AHI: %s", __FUNCTION__);

    if (1 == u16PacketLength)
    {
        u8TxPower = pu8LinkRxBuffer[ u32BytesRead ];
        u32BytesRead += sizeof(u8TxPower);

        /*
         * rev4 canonical TX-power SET validation.
         *
         * Established by physical HIL + MiniMac disassembly: in the linked
         * MiniMac path the TX-power PIB is a 6-bit two's-complement code. The
         * hardware only round-trips the exact codes it can represent; several
         * inputs are silently mangled and MUST be rejected rather than
         * accepted-then-clamped:
         *
         *   - 0x00..0x0A : accepted, positive levels 0..10 (round-trips).
         *   - 0x0B..0x1F : REJECTED (not exactly representable; would clamp).
         *   - 0x20..0x3F : accepted, 6-bit negatives -32..-1 (round-trips).
         *   - 0x40 and above : REJECTED. 0x40 is NOT round-trippable: SET
         *                      sign-extends the low 6 bits to 0, and GET then
         *                      returns a sign-extended i8, so the value written
         *                      cannot be read back. (Older rev3 code accepted
         *                      up to PHY_PIB_TX_POWER_MAX/0x40 — that was wrong.)
         *
         * Only exact, non-clamping codes are accepted. On rejection the outer
         * dispatch still emits the stock E_SL_MSG_STATUS frame and omits the
         * value frame, so the failed-set wire shape is unchanged (status only).
         */
        if (bAPP_AHIIsValidTxPower(u8TxPower))
        {
             /*
              * A readable rollback value is mandatory. Without it a later PDM
              * failure could report failure while leaving the radio changed.
              */
             if (eAppApiPlmeGet(PHY_PIB_ATTR_TX_POWER, &u32OldTxPower) ==
                     PHY_ENUM_SUCCESS &&
                 eAppApiPlmeSet(PHY_PIB_ATTR_TX_POWER, u8TxPower) ==
                     PHY_ENUM_SUCCESS)
             {
                /*
                 * Do not persist a value unless the radio accepted it without
                 * clamping. A successful command means both runtime and PDM
                 * state were updated; otherwise restore the previous PIB when
                 * it was available and keep the stock failure wire shape.
                 */
                if (eAppApiPlmeGet(PHY_PIB_ATTR_TX_POWER, &u32ReadBack) ==
                        PHY_ENUM_SUCCESS &&
                    (uint8)(u32ReadBack & PHY_PIB_TX_POWER_MASK) == u8TxPower &&
                    bAPP_AHISaveTxPower(u8TxPower))
                {
                    *peAHIStatus = E_AHI_SUCCESS;
                }
                else
                {
                    (void)eAppApiPlmeSet(PHY_PIB_ATTR_TX_POWER, u32OldTxPower);
                }
            }
        }
    }
}

/****************************************************************************
 *
 * NAME: vAPP_AHIGetTxPower
 *
 * DESCRIPTION:
 * Command to get the Tx Power
 *
 * Params:
 * @pu32TxPower: pointer for returning TX power level.
 *
 *
 ****************************************************************************/
PRIVATE void vAPP_AHIGetTxPower(uint32 *pu32TxPower, eAHI_Status *peAHIStatus)
{
    *peAHIStatus = E_AHI_PARSE_ERROR;

    DBG_vPrintf(TRUE, "AHI: %s", __FUNCTION__);

    if (eAppApiPlmeGet(PHY_PIB_ATTR_TX_POWER, pu32TxPower) == PHY_ENUM_SUCCESS)
    {
        *peAHIStatus = E_AHI_SUCCESS;
    }
}
/****************************************************************************/
/***        END OF FILE                                                   ***/
/****************************************************************************/
