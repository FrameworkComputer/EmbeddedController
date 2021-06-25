# SPDX-License-Identifier: Apache-2.0

zephyr_library_sources_ifdef(CONFIG_CROS_FLASH_IT8XXX2	cros_flash_it8xxx2.c)
zephyr_library_sources_ifdef(CONFIG_CROS_FLASH_NPCX	cros_flash_npcx.c)
