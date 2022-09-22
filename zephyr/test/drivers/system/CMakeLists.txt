# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Add source files
target_sources(app PRIVATE src/system.c)

target_sources_ifdef(CONFIG_PLATFORM_EC_BOARD_VERSION_CBI app PRIVATE
src/system_board_version_cbi.c)

target_sources_ifndef(CONFIG_PLATFORM_EC_BOARD_VERSION_CBI app PRIVATE
src/system_board_version_default.c)

target_sources_ifndef(CONFIG_HOST_CMD_AP_SET_SKUID app PRIVATE
src/system_not_board_ap_set_sku_id.c)
