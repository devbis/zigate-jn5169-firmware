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
 * MODULE:			SerialMAC_Lower.h
 *
 * COMPONENT:       SerialMAC
 *
 * DESCRIPTION:		Implements the Serial MAC Lower Layer (MAC Side) interface
 * 					as defined in 802.15.4 MAC Serial Interface V1.0 [doc142933].
 * 					This provides an interface to a Serial MAC Channel and
 * 					encapsulates the Remote Process Calls across the serial
 * 					link from the Serial MAC Upper Layer (Host Side) interface.
 *
 ****************************************************************************/

#ifndef  SMAC_LOWER_H_INCLUDED
#define  SMAC_LOWER_H_INCLUDED

#if defined __cplusplus
extern "C" {
#endif

/****************************************************************************/
/***        Include files                                                 ***/
/****************************************************************************/
#include <jendefs.h>
#include "AppApi.h"
#include "SMAC_Common.h"



/****************************************************************************/
/***        Type Definitions                                              ***/
/****************************************************************************/


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

PUBLIC bool bSML_AppInit(	uint8 u8Channel,
						PR_GET_BUFFER prMlmeGetBuffer,
              	  	   	PR_GET_BUFFER prMcpsGetBuffer,
              	  	   	PR_RELEASE_BUFFER prMlmeReleaseBuffer,
              	  	   	PR_RELEASE_BUFFER prMcpsReleaseBuffer);

PUBLIC bool bSML_ApiRun(void);

PUBLIC bool bSML_ApiSync(void);

PUBLIC void vSML_ApiErrorCallback(SMAC_API_ERROR_CALLBACK prCallback);



/****************************************************************************/
/***        Local Functions                                               ***/
/****************************************************************************/

#if defined __cplusplus
}
#endif

#endif  /* SMAC_LOWER_H_INCLUDED */

/****************************************************************************/
/***        END OF FILE                                                   ***/
/****************************************************************************/

