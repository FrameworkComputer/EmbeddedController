# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cros_ec_library_include_directories_ifdef(CONFIG_BOARD_KANIX include)
zephyr_include_directories("include")
zephyr_library_sources("src/charger.c" "src/usb_mux_config.c" "src/keyboard.c")
zephyr_library_sources("src/i2c_policy.c")
zephyr_library_sources("src/touch_panel.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/usbc_config.c")
