# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_sources(app PRIVATE src/usbc_alt_mode.c)
target_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY app PRIVATE src/usbc_alt_mode__require_ap_mode_entry.c)
target_sources_ifndef(CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY
	app PRIVATE src/usbc_alt_mode_ec_mode_entry.c)
