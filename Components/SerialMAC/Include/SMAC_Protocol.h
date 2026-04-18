/****************************************************************************
 *
 * Copyright 2020 NXP
 *
 * NXP Confidential. 
 * 
 * This software is owned or controlled by NXP and may only be used strictly 
 * in accordance with the applicable license terms.  
 * By expressly accepting such terms or by downloading, installing, activating 
 * and/or otherwise using the software, you are agreeing that you have read, 
 * and that you agree to comply with and are bound by, such license terms.  
 * If you do not agree to be bound by the applicable license terms, 
 * then you may not retain, install, activate or otherwise use the software. 
 * 
 *
 ****************************************************************************/


/****************************************************************************
 *
 * MODULE:			SMAC_Protocol.h
 *
 * COMPONENT:       SerialMAC
 *
 * DESCRIPTION:		Implements the MAC-Host Serial protocol as defined
 * 					in 802.15.4 Serial MAC Interface V1.0 [doc142933]
 *
 ****************************************************************************/

#ifndef  SMAC_PROTOCOL_H_INCLUDED
#define  SMAC_PROTOCOL_H_INCLUDED

#if defined __cplusplus
extern "C" {
#endif

/****************************************************************************/
/***        Include files                                                 ***/
/****************************************************************************/

#include <jendefs.h>
#include "SMAC_MsgTypes.h"

#define DEBUG_SERIALPROT	1

#if DEBUG_SERIALPROT
#define DEBUG_SP            TRUE
#else
#define DEBUG_SP            FALSE
#endif

/****************************************************************************/
/***        Macro Definitions                                             ***/
/****************************************************************************/

#define SP_MAX_PACKET_SIZE				255
#define SP_MAX_DEBUG_STRING				64
#define SP_SYNC_TIMEOUT_MS				2000


#define SP_COMMS_ERROR_NONE				0
#define SP_COMMS_ERROR_CRC				1
#define SP_COMMS_ERROR_PARITY			2
#define SP_COMMS_ERROR_FRAMING			3




/****************************************************************************/
/***        Type Definitions                                              ***/
/****************************************************************************/

typedef void (*SP_COMMS_ERROR_CALLBACK)(uint32);

/****************************************************************************/
/***        Local Function Prototypes                                     ***/
/****************************************************************************/

/****************************************************************************/
/***        Exported Variables                                            ***/
/****************************************************************************/

/****************************************************************************/
/***        Local Variables                                               ***/
/****************************************************************************/

/****************************************************************************/
/***        Exported Functions                                            ***/
/****************************************************************************/

PUBLIC void vSP_SetCommsErrorCallback(SP_COMMS_ERROR_CALLBACK prCommsError);
PUBLIC bool_t bSP_Sync(uint8 u8Channel);
PUBLIC bool_t bSP_ProcessIncoming(uint8 u8Channel, uint8 *pu8Type, uint8 *pu8Payload, uint16 *pu16Length, uint16 u16MaxLength);
PUBLIC bool_t bSP_WriteMessage(uint8 u8Channel, uint8 u8Type, uint8 *pu8Data, uint8 u8Length);
PUBLIC void vSP_Trace(uint8 u8Channel, string pszMessage);
PUBLIC void vSP_Traceu32Val(uint8 u8Channel, string pszMessage, uint32 u32Data);


/****************************************************************************/
/***        Local Functions                                               ***/
/****************************************************************************/

#if defined __cplusplus
}
#endif

#endif  /* SMAC_PROTOCOL_H_INCLUDED */

/****************************************************************************/
/***        END OF FILE                                                   ***/
/****************************************************************************/

