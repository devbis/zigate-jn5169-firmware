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
 * MODULE:			SMAC_Upper.h
 *
 * COMPONENT:       SerialMAC
 *
 * DESCRIPTION:		Implements the Serial MAC Upper Layer (Host Side) interface
 * 					as defined in 802.15.4 MAC Serial Interface V1.0 [doc142933]
 *					This provides the standard MAC API to the user while encapsulating
 *					the Remote Process Calls across the serial link to the Serial
 *					MAC Lower Layer (MAC Side) interface.
 *
 ****************************************************************************/

#ifndef  SMAC_UPPER_H_INCLUDED
#define  SMAC_UPPER_H_INCLUDED

#if defined __cplusplus
extern "C" {
#endif

/****************************************************************************/
/***        Include files                                                 ***/
/****************************************************************************/
#include <jendefs.h>
#include "AppApi.h"
#include "mac_sap.h"
#include "SMAC_Common.h"
#include "SMAC_Stats.h"

/****************************************************************************/
/***        Macro Definitions                                             ***/
/****************************************************************************/



/****************************************************************************/
/***        Type Definitions                                              ***/
/****************************************************************************/


/****************************************************************************/
/***        Exported Variables                                            ***/
/****************************************************************************/



/****************************************************************************/
/***        Exported Functions                                            ***/
/****************************************************************************/

typedef void (*tpfResetCallback) ( uint8 u8ResetReason , uint8 u8Length, uint8* pu8Buffer);
/* APP API */
PUBLIC void 	vSMU_SetChannel(uint8 u8Channel);

PUBLIC uint32 	u32SMU_ApiInit(	PR_GET_BUFFER prMlmeGetBuffer,
								PR_POST_CALLBACK prMlmeCallback,
								void* pvMlmeParam,
								PR_GET_BUFFER prMcpsGetBuffer,
								PR_POST_CALLBACK prMcpsCallback,
								void* pvMcpsParam);
PUBLIC bool_t 	bSMU_ApiRun(void);

PUBLIC bool_t 	bSMU_ApiSync(void);

PUBLIC void 	vSMU_ApiErrorCallback(SMAC_API_ERROR_CALLBACK prCallback);


PUBLIC bool_t 	bSMU_ApiGetMACStatus(SMAC_Status_s *psSMUStatus,
								 SMAC_Status_s *psSMLStatus);

PUBLIC bool_t 	bSMU_ApiSleep(uint8 u8Mode, uint32 u32DurationMs);

PUBLIC bool_t     bSMU_ApiSetAntennaInput (uint8 u8AntennaInput);

PUBLIC bool_t 	bSMU_EnterProgrammingMode(void);

PUBLIC void 	vSMU_ApiMlmeRequest(MAC_MlmeReqRsp_s *psMlmeReqRsp,
                   	   	   	    MAC_MlmeSyncCfm_s *psMlmeSyncCfm);

PUBLIC void 	vSMU_ApiMcpsRequest(MAC_McpsReqRsp_s *psMcpsReqRsp,
                   	   	   	   	MAC_McpsSyncCfm_s *psMcpsSyncCfm);


PUBLIC void 			vSMU_ApiSetSecurityMode(MAC_SecutityMode_e eSecurityMode);
PUBLIC void 			vSMU_ApiSetHighPowerMode(uint8 u8ModuleID, bool_t bMode);
PUBLIC void  			vSMU_AppApiSaveMacSettings(void);

PUBLIC void 			vSMU_ApiRestoreMacSettings(void);
PUBLIC bool_t 			bSMU_AppApi_CurrentlyScanning(void);
PUBLIC void 			vSMU_MAC_vPibSetPromiscuousMode(void *pvMac, bool_t bNewState, bool_t bInReset);

/* PLME Accessors */
PUBLIC PHY_Enum_e 		eSMU_AppApiPlmeSet(PHY_PibAttr_e ePhyPibAttribute,
                                 uint32 u32PhyPibValue);
PUBLIC PHY_Enum_e 		eSMU_AppApiPlmeGet(PHY_PibAttr_e ePhyPibAttribute,
                                 uint32 *pu32PhyPibValue);

/* MAC PIB Accessors */
PUBLIC SMAC_PibEnum_e 	eSMU_AppApiMlmeSet(SMAC_PibAttr_e eMacPibAttribute, void* pvMacPibData, uint8 u8Len);
PUBLIC SMAC_PibEnum_e 	eSMU_AppApiMlmeGet(SMAC_PibAttr_e eMacPibAttribute, void* pvMacPibData, uint8* pu8Len);

/* MAC PIB Helpers */
PUBLIC SMAC_PibEnum_e 	eSMU_AppApiMlmeSetU8(SMAC_PibAttr_e eMacPibAttribute, uint8 u8Val);
PUBLIC SMAC_PibEnum_e 	eSMU_AppApiMlmeSetU16(SMAC_PibAttr_e eMacPibAttribute, uint16 u16Val);
PUBLIC SMAC_PibEnum_e 	eSMU_AppApiMlmeSetU32(SMAC_PibAttr_e eMacPibAttribute, uint32 u32Val);

PUBLIC uint8 			u8SMU_AppApiMlmeGetU8(SMAC_PibAttr_e eMacPibAttribute);
PUBLIC uint16 			u16SMU_AppApiMlmeGetU16(SMAC_PibAttr_e eMacPibAttribute);
PUBLIC uint32 			u32SMU_AppApiMlmeGetU32(SMAC_PibAttr_e eMacPibAttribute);

PUBLIC uint8			u8SMU_ApiTXPowerSet(MAC_ExtAddr_s *psExtAddr, MAC_TxPower_s *psMacTxPower);
PUBLIC uint8 			u8SMU_ApiTXPowerGet(MAC_ExtAddr_s *psExtAddr, MAC_TxPower_s *psMacTxPower);
PUBLIC uint8 			u8SMU_ApiTXPowerDelete(MAC_ExtAddr_s *psExtAddr);
PUBLIC uint8			u8SMU_ApiTXPowerSetTable(const MAC_TxPowerTableEntry *psEntries, uint8 u8Index, uint8 u8NumEntries);
PUBLIC uint8 			u8SMU_ApiTXPowerGetTable(MAC_TxPowerTableEntry *psEntries, uint8 u8Index, uint8 u8NumEntries);
PUBLIC uint8 			u8SMU_ApiTXPowerClearTable(void);


PUBLIC uint8			u8SMU_ApiMibIeeeSetPolicy(uint8 u8Policy);
PUBLIC uint8			u8SMU_ApiMibIeeeGetPolicy(uint8 *pu8Policy);
PUBLIC uint8			u8SMU_ApiMibIeeeGetCount(uint8 *pu8Count);
PUBLIC uint8			u8SMU_ApiMibIeeeAddDevice(uint64 u64Address, uint8* pu8Count);
PUBLIC uint8			u8SMU_ApiMibIeeeSetTable(uint8 u8Index, uint8 u8Count, uint64 *pu64Address);
PUBLIC uint8			u8SMU_ApiMibIeeeGetTable(uint8 u8Index, uint8 u8Count, uint64 *pu64Address);
PUBLIC SMAC_PibEnum_e   eSMU_ApiMacPibGetProtoInfo(SMAC_PibAttr_e eMacPibAttribute, void* pvMacPibData, uint8* pu8Len);

PUBLIC void				vSMU_ApiResetSerialStats(void);
PUBLIC void				vSMU_ApiGetSerialStats(SMAC_Stats_s *psStats);
PUBLIC void             vSMU_ApiSetResetCallback( tpfResetCallback pfCallback);



#if defined __cplusplus
}
#endif

#endif  /* SMAC_UPPER_H_INCLUDED */

/****************************************************************************/
/***        END OF FILE                                                   ***/
/****************************************************************************/

