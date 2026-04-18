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
 * MODULE:			SMAC_Queue
 *
 * COMPONENT:       SerialMAC
 *
 * DESCRIPTION:		Implements a FIFO queue which is used in Serial MAC
 * 					to handle memory and queue events
 *
 ****************************************************************************/

#ifndef  SMAC_QUEUE_H_INCLUDED
#define  SMAC_QUEUE_H_INCLUDED

#if defined __cplusplus
extern "C" {
#endif

/****************************************************************************/
/***        Include files                                                 ***/
/****************************************************************************/

#include <jendefs.h>


/****************************************************************************/
/***        Type Definitions                                              ***/
/****************************************************************************/
/* Each FIFO queue has a pointer to the current read position and write
   position, and the start and end of the queue */
typedef struct
{
    void **ppvReadPtr;
    void **ppvWritePtr;
    void **ppvQueueStart;
    void **ppvQueueEnd;

} SM_FifoQueue_s;




/****************************************************************************/
/***        Exported Functions                                            ***/
/****************************************************************************/
PUBLIC void vSQ_FifoInit(SM_FifoQueue_s *psQueue, void **ppvDataStart, uint8 u8Entries);
PUBLIC void *pvSQ_FifoPull(SM_FifoQueue_s *psQueue);
PUBLIC bool bSQ_FifoPush(SM_FifoQueue_s *psQueue, void *pvData);



#if defined __cplusplus
}
#endif

#endif  /* SMAC_QUEUE_H_INCLUDED */

/****************************************************************************/
/***        END OF FILE                                                   ***/
/****************************************************************************/

