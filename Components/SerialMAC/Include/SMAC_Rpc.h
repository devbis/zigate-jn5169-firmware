/****************************************************************************
 *
 * Copyright 2020-2021 NXP
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
 * MODULE:			SMAC_Rpc
 *
 * COMPONENT:       SerialMAC
 *
 * DESCRIPTION:		Implements the Host/MAC Layer Serial Remote Procedure call
 * 				 	interface as defined in 802.15.4 MAC Serial Interface V1.0 [doc142933]
 *					Allows user to make Remote procedure calls which cause
 *					serial message exchange with remote to pass/return parameters.
 *					And handle indications, confirms, delayed confirms and
 *					responses.
 *
 ****************************************************************************/

#ifndef  SMAC_RPC_H_INCLUDED
#define  SMAC_RPC_H_INCLUDED

#if defined __cplusplus
extern "C" {
#endif

/****************************************************************************/
/***        Include files                                                 ***/
/****************************************************************************/

#include <jendefs.h>
#include "SMAC_MsgTypes.h"
#include "SMAC_Protocol.h"


/****************************************************************************/
/***        Type Definitions                                              ***/
/****************************************************************************/
typedef bool_t (*RPC_POST_CALLBACK)(uint8, void *, uint16);


/****************************************************************************/
/***        Macro Definitions                                             ***/
/****************************************************************************/

#define SRPC_MAX_PACKET_SIZE				SP_MAX_PACKET_SIZE

/* RPC Timeouts
 * ------------
 * Give the nature of RPC there is a call and a corresponding return. After
 * making an API call the RPC will wait for a response. In the case of
 * communication errors we set a maxium time to wait (in milliseconds).
 */
#define SRPC_CALL_DEFAULT_TIMEOUT_MS		100 /* 100 millisecond RPC timeout */



/****************************************************************************/
/***        Exported Variables                                            ***/
/****************************************************************************/

PUBLIC bool_t 	bSRPC_Init(	uint8 u8Channel,
							uint8 *pu8Buffer,
							uint16 u16Size,
							RPC_POST_CALLBACK prCallBack);

PUBLIC bool_t 	bSRPC_Run(uint8 u8Channel);
PUBLIC void 	vSRPC_Close(uint8 u8Channel);

PUBLIC bool_t		bSRPC_Sync(uint8 u8Channel);

PUBLIC bool_t 	bSRPC_MakeCall(	uint8 u8Channel,
								uint8 u8RPCIDCall, uint8 u8RPCIDRet,
								void *pvInParam, uint16 u16InLen,
								void *pvOutParam, uint16 *pu16OutLen,
								uint16 u16MaxOutLen);

PUBLIC bool_t		bSRPC_SendEvent(uint8 u8Channel, uint8 u8RPCId, void *pvParam, uint16 u16ParamLen,  bool_t bISRDisable);


PUBLIC void		vSRPC_SendInfoMsg(uint8 u8Channel, string pszMsg);
PUBLIC void		vSRPC_SendAPINotSupportedMsg(uint8 u8Channel);


PUBLIC uint32 	vRPC_GetTimeout(void);
PUBLIC void		vRPC_SetTimeout(uint32 u32TimeoutMs);

PUBLIC void 	vRPC_SetErrorCallback(SP_COMMS_ERROR_CALLBACK prCommsError);




#if defined __cplusplus
}
#endif

#endif  /* SMAC_RPC_H_INCLUDED */

/****************************************************************************/
/***        END OF FILE                                                   ***/
/****************************************************************************/

