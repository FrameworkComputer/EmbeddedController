# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if (DEFINED CONFIG_SOC_FAMILY_NPCX)
	add_subdirectory(npcx)
elseif (DEFINED CONFIG_SOC_FAMILY_RISCV_ITE)
	add_subdirectory(it8xxx2)
elseif (DEFINED CONFIG_SOC_FAMILY_MEC)
	add_subdirectory(mchp)
elseif (DEFINED CONFIG_SOC_FAMILY_STM32)
	add_subdirectory(stm32)
else ()
	add_subdirectory(stub)
endif()
