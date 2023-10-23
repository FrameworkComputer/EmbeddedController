# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Add include paths
target_include_directories(app PRIVATE
    "${PLATFORM_EC}/zephyr/shim/src/led_driver")

# Add source files
zephyr_library_sources_ifdef(CONFIG_LINK_TEST_SUITE_LED_DRIVER
    src/led_common.c
    src/led.c
)
zephyr_library_sources_ifdef(CONFIG_LINK_TEST_SUITE_LED_DRIVER_ALIAS_LED
    src/led_alias.c
)
