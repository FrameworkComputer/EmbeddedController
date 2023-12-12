# SPDX-License-Identifier: Apache-2.0

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CBI        cros_cbi.c
                                                           cros_cbi_fw_config.c
                                                           cros_cbi_ssfc.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CBI_TRANSFER_EEPROM_FLASH cbi_transfer.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CBI_EEPROM cbi_eeprom.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CBI_FLASH cbi_flash.c)
