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
 * MODULE:			SMAC_Stats
 *
 * COMPONENT:       SerialMAC
 *
 * DESCRIPTION:		Implements statistic and debug info for SerialMAC interface
 *
 ****************************************************************************/

#ifndef  SMAC_STATS_H_INCLUDED
#define  SMAC_STATS_H_INCLUDED

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
/***        Macro Definitions                                             ***/
/****************************************************************************/
#ifndef SMAC_INLINE
#define SMAC_INLINE INLINE
#endif

#ifndef SMAC_ALWAYS_INLINE
#define SMAC_ALWAYS_INLINE ALWAYS_INLINE
#endif


/****************************************************************************/
/***        Type Definitions                                              ***/
/****************************************************************************/
typedef struct
{
	uint32	u32TxBytes;
	uint32 	u32RxBytes;
	uint16	u16RxFifoHigh;
	uint16	u16TxFifoHigh;
	uint8	u8CRCError;
	uint8 	u8TXFull;
	uint8   u8RxHwErr;
}SMAC_Stats_s;

/****************************************************************************/
/***        Exported Variables                                            ***/
/****************************************************************************/
extern SMAC_Stats_s	s_sSMAC_Stats;


/****************************************************************************/
/***        Exported Functions                                            ***/
/****************************************************************************/
PUBLIC void 	vSSTAT_Reset(void);
PUBLIC void 	vSSTAT_Get(SMAC_Stats_s *psStats);

/****************************************************************************/
/***        Inline Functions                                              ***/
/****************************************************************************/

SMAC_INLINE void vSTAT_Tx(void) SMAC_ALWAYS_INLINE;
SMAC_INLINE void vSTAT_Tx(void)
{
	s_sSMAC_Stats.u32TxBytes++;
}
SMAC_INLINE void vSTAT_Rx(void) SMAC_ALWAYS_INLINE;
SMAC_INLINE void vSTAT_Rx(void)
{
	s_sSMAC_Stats.u32RxBytes++;
}
SMAC_INLINE void vSTAT_RxFifo(uint16 u16Rx) SMAC_ALWAYS_INLINE;
SMAC_INLINE void vSTAT_RxFifo(uint16 u16Rx)
{
	if(u16Rx > s_sSMAC_Stats.u16RxFifoHigh)
	{
		s_sSMAC_Stats.u16RxFifoHigh = u16Rx;
	}
}
SMAC_INLINE void vSTAT_TxFifo(uint16 u16Tx) SMAC_ALWAYS_INLINE;
SMAC_INLINE void vSTAT_TxFifo(uint16 u16Tx)
{
	if(u16Tx > s_sSMAC_Stats.u16TxFifoHigh)
	{
		s_sSMAC_Stats.u16TxFifoHigh = u16Tx;
	}
}
SMAC_INLINE void vSTAT_CRCError(void) SMAC_ALWAYS_INLINE;
SMAC_INLINE void vSTAT_CRCError(void)
{
	s_sSMAC_Stats.u8CRCError++;
}
SMAC_INLINE void vSTAT_TxFull(void) SMAC_ALWAYS_INLINE;
SMAC_INLINE void vSTAT_TxFull(void)
{
	s_sSMAC_Stats.u8TXFull++;
}

SMAC_INLINE void vSTAT_RxHwErr(void) SMAC_ALWAYS_INLINE;
SMAC_INLINE void vSTAT_RxHwErr(void)
{
    s_sSMAC_Stats.u8RxHwErr++;
}


#if defined __cplusplus
}
#endif

#endif  /* SMAC_STATS_H_INCLUDED */

/****************************************************************************/
/***        END OF FILE                                                   ***/
/****************************************************************************/

