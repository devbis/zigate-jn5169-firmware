/*****************************************************************************
 *
 * MODULE:             JN-AN-1247
 *
 * COMPONENT:          app_green_power.h
 *
 ****************************************************************************
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
 *
 ***************************************************************************/

#ifndef APP_GREEN_POWER_H
#define APP_GREEN_POWER_H

#include "gp.h"


/****************************************************************************/
/***        External Variables                                            ***/
/****************************************************************************/


/****************************************************************************/
/***        Exported Functions                                            ***/
/****************************************************************************/

void vApp_GP_RegisterDevice(tfpZCL_ZCLCallBackFunction fptrEPCallBack);
void vApp_GP_EnterCommissioningMode(void);

/* Explicit, bounded local Green Power proxy commissioning control.
 *
 * bEnable == TRUE  : open the local proxy commissioning window for
 *                    u8TimeoutSeconds (1..255) and broadcast ZGP Proxy
 *                    Commissioning Mode (enter, exit-on-window-expiration).
 * bEnable == FALSE : close the window immediately and broadcast the exit
 *                    command; u8TimeoutSeconds is ignored.
 *
 * *pu8EffectiveTimeoutSeconds (optional) receives the window actually
 * programmed, in seconds; 0 on failure or when disabling.
 *
 * Returns a teZCL_Status value (E_ZCL_SUCCESS == 0 on success). */
uint8 u8App_GP_SetProxyCommissioningMode(bool_t  bEnable,
                                         uint8   u8TimeoutSeconds,
                                         uint8  *pu8EffectiveTimeoutSeconds);
void vAPP_GP_LoadPDMData(void);
void vHandleGreenPowerEvent(tsGP_GreenPowerCallBackMessage *psGPMessage);
void vAPP_GP_ResetData(void);
/****************************************************************************/
/***        END OF FILE                                                   ***/
/****************************************************************************/

#endif /* APP_GREEN_POWER_H */
