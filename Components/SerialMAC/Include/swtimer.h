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
 * MODULE:			Serial MAC
 *
 * COMPONENT:       Software Timer API derived from H/W Tick Timer
 *
 * DESCRIPTION:		Implements simple millisecond software timer API using
 * 					the Tick Timer
 *
 ****************************************************************************/

#ifndef  SWTIMER_H_INCLUDED
#define  SWTIMER_H_INCLUDED

#if defined __cplusplus
extern "C" {
#endif

/****************************************************************************/
/***        Include files                                                 ***/
/****************************************************************************/

#include <jendefs.h>


/****************************************************************************/
/***        Macro Definitions                                             ***/
/****************************************************************************/

/* Generates 1ms interrupt on 16Mhz clock devices */
#define E_SWTIMER_INTERVAL_MS		16000


/****************************************************************************/
/***        Type Definitions                                              ***/
/****************************************************************************/

typedef volatile uint32	SWTimer;

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

PUBLIC void vSWTimer_Init(void);
PUBLIC void vSWTimer_Close(void);
PUBLIC void vSWTimer_Set(SWTimer *pswT, uint32 u32ms);
PUBLIC bool_t bSWTimer_Expired(SWTimer *pswT);
PUBLIC void vSWTimer_Kill(SWTimer *pswT);
PUBLIC uint32 u32SWTimer_Now(void);

/****************************************************************************/
/***        Local Functions                                               ***/
/****************************************************************************/

#if defined __cplusplus
}
#endif

#endif  /* SWTIMER_H_INCLUDED */

/****************************************************************************/
/***        END OF FILE                                                   ***/
/****************************************************************************/

