# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources(app PRIVATE
	src/flash.c
)

target_sources_ifdef(CONFIG_PLATFORM_EC_USE_ZEPHYR_FLASH_PAGE_LAYOUT
	app PRIVATE src/flash_bank_api.c
)
