# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Add source files
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_CBI_FLASH
			app PRIVATE src/cbi_flash.c)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_CBI_FLASH_FROM_EEPROM
			app PRIVATE src/cbi_flash_from_eeprom.c)
