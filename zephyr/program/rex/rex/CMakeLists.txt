# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cros_ec_library_include_directories_ifdef(CONFIG_BOARD_REX include)
zephyr_include_directories("include")
zephyr_library_sources("src/usb_mux_config.c")
zephyr_library_sources("src/i2c_policy.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/usbc_config.c")
