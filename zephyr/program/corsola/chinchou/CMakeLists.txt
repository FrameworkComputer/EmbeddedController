# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a
# BSD-style license that can be found in the LICENSE file.

# Chinchou Makeifile
zephyr_include_directories("include")
zephyr_library_sources("../src/ite_hooks.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_I2C "../src/ite_i2c.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC
                             "../src/ite_usb_pd_policy.c" "../src/ite_usbc.c")
zephyr_library_sources("src/ppc.c")
zephyr_library_sources("src/board.c")
zephyr_library_sources("src/thermal.c")
