###############################################################################
#
# MODULE:   Config_ZBPro.mk for 516x and 7x chip families
#
# DESCRIPTION: ZBPro stack configuration. Defines tool, library and
#              header file details for building an app using the ZBPro stack
#
###############################################################################
# This software is owned by NXP B.V. and/or its supplier and is protected
# under applicable copyright laws. All rights are reserved. We grant You,
# and any third parties, a license to use this software solely and
# exclusively on NXP products [NXP Microcontrollers such as JN514x, JN516x, JN517x].
# You, and any third parties must reproduce the copyright and warranty notice
# and any other legend of ownership on each  copy or partial copy of the software.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
# Copyright 2015-2019, 2022 NXP
#
###############################################################################

###############################################################################
# Tools

WWAH ?= 0
LEGACY ?= 0
R23_UPDATES ?= 0
ifeq ($(R23_UPDATES),1)
ifeq ($(LEGACY),0)
    R22PLUS = _R23
else
    $(info ***** Conflicting R23 settings, probably building the GU *****)
endif
endif
OTA ?= 0
OTA_INTERNAL_STORAGE ?= 0

PDUMCONFIG = $(TOOL_BASE_DIR)/PDUMConfig/Source/PDUMConfig
ZPSCONFIG  = $(TOOL_BASE_DIR)/ZPSConfig/Source/ZPSConfig


STACK_SIZE ?= 5000
MINIMUM_HEAP_SIZE ?= 2000
###############################################################################
# ROM based software components

INCFLAGS += -I$(COMPONENTS_BASE_DIR)/Mac/Include
INCFLAGS += -I$(COMPONENTS_BASE_DIR)/MicroSpecific/Include
INCFLAGS += -I$(COMPONENTS_BASE_DIR)/MiniMAC/Include
INCFLAGS += -I$(COMPONENTS_BASE_DIR)/MMAC/Include
INCFLAGS += -I$(COMPONENTS_BASE_DIR)/TimerServer/Include
INCFLAGS += -I$(COMPONENTS_BASE_DIR)/Random/Include
INCFLAGS += -I$(COMPONENTS_BASE_DIR)/ZigbeeCommon/Include
INCFLAGS += -I$(COMPONENTS_BASE_DIR)/platform
INCFLAGS += -I$(SDK_BASE_DIR)/framework/Common
INCFLAGS += -I$(SDK_BASE_DIR)/framework/RNG/Interface

ifeq ($(JENNIC_MAC), MAC)
    $(info JENNIC_MAC is MAC )
    APPLIBS +=ZPSMAC
    CFLAGS  += -DREDUCED_ZIGBEE_MAC_BUILD
    REDUCED_MAC_LIB_SUFFIX = ZIGBEE_
else
    $(info JENNIC_MAC is Mini MAC shim )
    JENNIC_MAC = MiniMacShim
    JENNIC_MAC_PLATFORM ?= SOC

###############################################################################
# Determine correct MAC library for platform

    ifeq ($(JENNIC_MAC_PLATFORM),SOC)
        $(info JENNIC_MAC_PLATFORM is SOC)
        APPLIBS +=ZPSMAC_Mini_SOC
    else
        ifeq ($(JENNIC_MAC_PLATFORM),SERIAL)
            $(info JENNIC_MAC_PLATFORM is SERIAL)
            APPLIBS +=ZPSMAC_Mini_SERIAL
            APPLIBS +=SerialMiniMacUpper
        else
            ifeq ($(JENNIC_MAC_PLATFORM),MULTI)
                 $(info JENNIC_MAC_PLATFORM is MULTI)
                 APPLIBS +=ZPSMAC_Mini_MULTI
                 APPLIBS +=SerialMiniMacUpper
            endif
       endif
    endif
endif


ifeq ($(WWAH),1)
    CFLAGS += -DWWAH_SUPPORT
endif

ifeq ($(R23_UPDATES),1)
    CFLAGS += -DR23_UPDATES
endif

###############################################################################
# RAM based software components


CFLAGS += -DPDM_USER_SUPPLIED_ID
CFLAGS += -DPDM_NO_RTOS
ifeq ($(PDM_BUILD_TYPE),_EEPROM)
    CFLAGS += -DPDM$(PDM_BUILD_TYPE)
else
    ifeq ($(PDM_BUILD_TYPE),_EXTERNAL_FLASH)
        CFLAGS += -DPDM$(PDM_BUILD_TYPE)
    else
        ifeq ($(PDM_BUILD_TYPE),_NONE)
            CFLAGS += -DPDM$(PDM_BUILD_TYPE)
        else
            $(error PDM_BUILD_TYPE must be defined please define PDM_BUILD_TYPE=_EEPROM or PDM_BUILD_TYPE=_EXTERNAL_FLASH)
        endif
    endif
endif


# NB Order is significant for GNU linker

APPLIBS +=PWRM
APPLIBS +=ZPSTSV
APPLIBS +=AES_SW
APPLIBS +=PDUM
ifeq ($(WWAH),0)
    ifeq ($(LEGACY),0)
        APPLIBS +=ZPSAPL$(R22PLUS)
    else
        APPLIBS +=ZPSAPL_LEGACY
        CFLAGS +=  -DLEGACY_SUPPORT
    endif
else
    APPLIBS +=ZPSAPL_WWAH
endif

APPLIBS +=Random


INCFLAGS += $(addsuffix /Include,$(addprefix -I$(COMPONENTS_BASE_DIR)/,$(APPLIBS)))
INCFLAGS += -I$(COMPONENTS_BASE_DIR)/PDM/Include

ifneq ($(PDM_BUILD_TYPE),_NONE)
    APPLIBS +=PDM$(PDM_BUILD_TYPE)_NO_RTOS
endif

ifeq ($(TRACE), 1)
    CFLAGS  += -DDBG_ENABLE
    $(info Building trace version ...)
    APPLIBS +=DBG
else
    INCFLAGS += -I$(COMPONENTS_BASE_DIR)/DBG/Include
endif

ifeq ($(OPTIONAL_STACK_FEATURES),1)
    ifneq ($(ZBPRO_DEVICE_TYPE), ZED)
        APPLIBS += ZPSIPAN
    else
        APPLIBS += ZPSIPAN_ZED
    endif
endif

ifeq ($(OPTIONAL_STACK_FEATURES),2)
    ifneq ($(ZBPRO_DEVICE_TYPE), ZED)
        APPLIBS += ZPSGP
    else
        APPLIBS += ZPSGP_ZED
    endif
endif

ifeq ($(OPTIONAL_STACK_FEATURES),3)
    ifneq ($(ZBPRO_DEVICE_TYPE), ZED)
        APPLIBS += ZPSGP
        APPLIBS += ZPSIPAN
    else
        APPLIBS += ZPSGP_ZED
        APPLIBS += ZPSIPAN_ZED
    endif
endif


###############################################################################
# Paths to components provided as source

APPSRC += ZQueue.c
APPSRC += ZTimer.c
APPSRC += app_zps_link_keys.c
ifeq ($(R23_UPDATES),1)
APPSRC += tlv.c
endif
###############################################################################
# Paths to network and application layer libs for stack config tools

INCFLAGS += -I$(COMPONENTS_BASE_DIR)/ZPSMAC/Include
INCFLAGS += -I$(COMPONENTS_BASE_DIR)/ZPSNWK/Include
INCFLAGS += -I$(COMPONENTS_BASE_DIR)/ZPSAPL/Include
INCFLAGS += -I$(COMPONENTS_BASE_DIR)/ZigbeeCommon/Include
ifeq ($(ZBPRO_DEVICE_TYPE), ZCR)
    ifeq ($(WWAH),0)
        APPLIBS +=ZPSNWK$(R22PLUS)
    else
        APPLIBS +=ZPSNWK_WWAH
    endif
else
    ifeq ($(ZBPRO_DEVICE_TYPE), ZED)
        ifeq ($(WWAH),0)
            APPLIBS +=ZPSNWK_ZED$(R22PLUS)
        else
            APPLIBS +=ZPSNWK_WWAH_ZED
        endif
    else
        $(error ZBPRO_DEVICE_TYPE must be set to either ZCR or ZED)
    endif
endif

ifeq ($(ZBPRO_DEVICE_TYPE), ZCR)
    ifeq ($(WWAH),0)
        ZPS_NWK_LIB = $(COMPONENTS_BASE_DIR)/Library/libZPSNWK$(R22PLUS)_$(JENNIC_CHIP_FAMILY).a
    else
        ZPS_NWK_LIB = $(COMPONENTS_BASE_DIR)/Library/libZPSNWK_WWAH_$(JENNIC_CHIP_FAMILY).a
    endif
endif
ifeq ($(ZBPRO_DEVICE_TYPE), ZED)
    ifeq ($(WWAH),0)
        ZPS_NWK_LIB = $(COMPONENTS_BASE_DIR)/Library/libZPSNWK_ZED$(R22PLUS)_$(JENNIC_CHIP_FAMILY).a
    else
        ZPS_NWK_LIB = $(COMPONENTS_BASE_DIR)/Library/libZPSNWK_WWAH_ZED_$(JENNIC_CHIP_FAMILY).a
    endif
endif

ifeq ($(WWAH),0)
    ifeq ($(LEGACY),0)
        ZPS_APL_LIB = $(COMPONENTS_BASE_DIR)/Library/libZPSAPL$(R22PLUS)_$(JENNIC_CHIP_FAMILY).a
    else
        ZPS_APL_LIB = $(COMPONENTS_BASE_DIR)/Library/libZPSAPL_LEGACY_$(JENNIC_CHIP_FAMILY).a
    endif
else
    ZPS_APL_LIB = $(COMPONENTS_BASE_DIR)/Library/libZPSAPL_WWAH_$(JENNIC_CHIP_FAMILY).a
endif
LDFLAGS += -Wl,--gc-sections

#############################LEGACY CHIP END########################################

