# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if (DEFINED CONFIG_SOC_FAMILY_NPCX)
	add_subdirectory(npcx)
elseif (DEFINED CONFIG_SOC_FAMILY_ITE_EC)
	add_subdirectory(it8xxx2)
elseif (DEFINED CONFIG_SOC_FAMILY_MICROCHIP_MEC)
	add_subdirectory(mchp)
elseif (DEFINED CONFIG_SOC_FAMILY_STM32)
	add_subdirectory(stm32)
elseif (DEFINED CONFIG_SOC_FAMILY_REALTEK_EC)
	add_subdirectory(rtk)
elseif (DEFINED CONFIG_SOC_EGIS_ET171)
	add_subdirectory(egis)
elseif (DEFINED CONFIG_SOC_EM32F967)
	add_subdirectory(elan)
elseif (DEFINED CONFIG_SOC_SERIES_FT90)
	add_subdirectory(ft90)
else ()
	add_subdirectory(stub)
endif()
