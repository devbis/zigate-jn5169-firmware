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
 * MODULE:			SMAC_Uart.h
 *
 * COMPONENT:       SerialMAC
 *
 * DESCRIPTION:		Implements the UART FIFO Serial interface as part of the
 * 					MAC-Host Serial Interface as defined in 802.15.4 MAC
 * 					Serial Interface V1.0 [doc142933]
 * 					2-Wire Mode. No Flow Control. 8 Data Bits. No parity.
 *
 ****************************************************************************/

#ifndef  SMAC_UART_H_INCLUDED
#define  SMAC_UART_H_INCLUDED

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


#define SMUART_TX_FIFO_SIZE					512ul
#define SMUART_RX_FIFO_SIZE				 	512ul
#define SMUART_RX_FIFO_THRESHOLD			400ul	/* 80% */




/****************************************************************************/
/***        Type Definitions                                              ***/
/****************************************************************************/
/** Enumerated list of supported baud rates */
typedef enum eUF_BR_tag
{
	eBaud1M,
	eBaud9600,
	eBaudDefault
} eSMUart_BR;


typedef struct sUF_Buffer_tag
{
	uint8 *pu8Tx;
	uint8 *pu8Rx;
	uint16 u16TxSize;
	uint16 u16RxSize;

} sSMUart_Fifo;


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
PUBLIC bool bSMUart_FifoOpen(uint8 u8Uart, eSMUart_BR eBR);
PUBLIC void vSMUart_FifoConfigure(uint8 u8Uart, eSMUart_BR eBR);
PUBLIC void	vSMUart_FifoClose(uint8 u8Uart);
PUBLIC bool bSMUart_FifoWriteByte(uint8 u8Uart, uint8 u8Byte);
PUBLIC bool vSMUart_FifoWriteString(uint8 u8Uart, string pszText);
PUBLIC bool bSMUart_FifoRxPending(uint8 u8Uart);
PUBLIC void	vSMUart_FifoRxPurge(uint8 u8Uart);

/****************************************************************************/
/***        Local Functions                                               ***/
/****************************************************************************/

#if (JENNIC_CHIP_FAMILY == JN518x)

#define E_AHI_UART_0                    (0)
#define E_AHI_UART_1                    (1)

/* Value enumerations: UART */
#define E_AHI_UART_RATE_4800            (0)
#define E_AHI_UART_RATE_9600            (1)
#define E_AHI_UART_RATE_19200           (2)
#define E_AHI_UART_RATE_38400           (3)
#define E_AHI_UART_RATE_76800           (4)
#define E_AHI_UART_RATE_115200          (5)
#define E_AHI_UART_WORD_LEN_5           (0)
#define E_AHI_UART_WORD_LEN_6           (1)
#define E_AHI_UART_WORD_LEN_7           (2)
#define E_AHI_UART_WORD_LEN_8           (3)
#define E_AHI_UART_FIFO_LEVEL_1         (0)
#define E_AHI_UART_FIFO_LEVEL_4         (1)
#define E_AHI_UART_FIFO_LEVEL_8         (2)
#define E_AHI_UART_FIFO_LEVEL_14        (3)
#define E_AHI_UART_LS_ERROR             (0x80)
#define E_AHI_UART_LS_TEMT              (0x40)
#define E_AHI_UART_LS_THRE              (0x20)
#define E_AHI_UART_LS_BI                (0x10)
#define E_AHI_UART_LS_FE                (0x08)
#define E_AHI_UART_LS_PE                (0x04)
#define E_AHI_UART_LS_OE                (0x02)
#define E_AHI_UART_LS_DR                (0x01)
#define E_AHI_UART_MS_CTS               (0x10)
#define E_AHI_UART_MS_DCTS              (0x01)
#define E_AHI_UART_INT_MODEM            (0)
#define E_AHI_UART_INT_TX               (1)
#define E_AHI_UART_INT_RXDATA           (2)
#define E_AHI_UART_INT_RXLINE           (3)
#define E_AHI_UART_INT_TIMEOUT          (6)
#define E_AHI_UART_TX_RESET             (TRUE)
#define E_AHI_UART_RX_RESET             (TRUE)
#define E_AHI_UART_TX_ENABLE            (FALSE)
#define E_AHI_UART_RX_ENABLE            (FALSE)
#define E_AHI_UART_EVEN_PARITY          (TRUE)
#define E_AHI_UART_ODD_PARITY           (FALSE)
#define E_AHI_UART_PARITY_ENABLE        (TRUE)
#define E_AHI_UART_PARITY_DISABLE       (FALSE)
#define E_AHI_UART_1_STOP_BIT           (TRUE)
#define E_AHI_UART_2_STOP_BITS          (FALSE)
#define E_AHI_UART_RTS_HIGH             (TRUE)
#define E_AHI_UART_RTS_LOW              (FALSE)
#define E_AHI_UART_FIFO_ARTS_LEVEL_8    (0)
#define E_AHI_UART_FIFO_ARTS_LEVEL_11   (1)
#define E_AHI_UART_FIFO_ARTS_LEVEL_13   (2)
#define E_AHI_UART_FIFO_ARTS_LEVEL_15   (3)
typedef enum
{
    DBG_E_UART_0,
    DBG_E_UART_1

} DBG_teUart;

typedef enum
{
    DBG_E_UART_BAUD_RATE_4800,
    DBG_E_UART_BAUD_RATE_9600,
    DBG_E_UART_BAUD_RATE_19200,
    DBG_E_UART_BAUD_RATE_38400,
    DBG_E_UART_BAUD_RATE_76800,
    DBG_E_UART_BAUD_RATE_115200

} DBG_teUartBaudRate;

PUBLIC void vAHI_UartSetRTSCTS(uint8 u8Uart, bool_t bRTSCTSEn);
PUBLIC bool_t bAHI_UartEnable(
    uint8       u8Uart,
    uint8      *pu8TxBufAd,
    uint16      u16TxBufLen,
    uint8      *pu8RxBufAd,
    uint16      u16RxBufLen);
PUBLIC void vAHI_UartSetBaudDivisor(
    uint8       u8Uart,
    uint16      u16Divisor);
PUBLIC void vAHI_UartSetClocksPerBit(
    uint8       u8Uart,
    uint8       u8Cpb);
PUBLIC void vAHI_UartSetControl(
    uint8       u8Uart,
    bool_t      bEvenParity,
    bool_t      bEnableParity,
    uint8       u8WordLength,
    bool_t      bOneStopBit,
    bool_t      bRtsValue);
PUBLIC void vAHI_UartDisable(uint8 u8Uart);
PUBLIC uint16  u16AHI_UartReadTxFifoLevel(
    uint8       u8Uart);
PUBLIC void vAHI_UartWriteData(
    uint8       u8Uart,
    uint8       u8Data);
PUBLIC uint8 u8AHI_UartReadData(
    uint8       u8Uart);
PUBLIC void vAHI_UartSetBaudRate(
		DBG_teUart       eUart,
		DBG_teUartBaudRate eBaudRate);
PUBLIC uint16  u16AHI_UartReadRxFifoLevel(
    uint8       u8Uart);
#endif
#if defined __cplusplus
}
#endif

#endif  /* SMAC_UART_H_INCLUDED */

/****************************************************************************/
/***        END OF FILE                                                   ***/
/****************************************************************************/

