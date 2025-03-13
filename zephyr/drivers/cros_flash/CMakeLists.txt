# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_CROS_FLASH_IT8XXX2	cros_flash_it8xxx2.c)
zephyr_library_sources_ifdef(CONFIG_CROS_FLASH_NPCX	cros_flash_npcx.c)
zephyr_library_sources_ifdef(CONFIG_CROS_FLASH_XEC	cros_flash_xec.c)
zephyr_library_sources_ifdef(CONFIG_CROS_FLASH	        cros_flash.c)
zephyr_library_sources_ifdef(CONFIG_CROS_FLASH_BACKEND_STM32
	                                                cros_flash_stm32_backend.c)
