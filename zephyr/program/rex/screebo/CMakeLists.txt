# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cros_ec_library_include_directories_ifdef(CONFIG_BOARD_SCREEBO include)
zephyr_include_directories("include")
zephyr_library_sources("src/board.c"
                       "src/i2c_policy.c"
                       "src/keyboard.c"
                       "src/usb_config.c")
