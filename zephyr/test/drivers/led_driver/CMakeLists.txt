# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Add include paths
target_include_directories(app PRIVATE
    "${PLATFORM_EC}/zephyr/shim/src/led_driver"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

# Add source files
zephyr_library_sources_ifdef(CONFIG_LINK_TEST_SUITE_LED_DRIVER
    src/led_common.c
    src/led.c
    src/stubs.c
)
zephyr_library_sources_ifdef(CONFIG_LINK_TEST_SUITE_LED_DRIVER_ALIAS_LED
    src/led_alias.c
)
zephyr_library_sources_ifdef(CONFIG_LINK_TEST_SUITE_LED_DRIVER_CUSTOM
    src/led_custom.c
    src/stubs.c
)
zephyr_library_sources_ifdef(CONFIG_LINK_TEST_SUITE_LED_DRIVER_SEQUENCE
    src/led_sequence.c
    src/stubs.c
)
zephyr_library_sources_ifdef(CONFIG_LINK_TEST_SUITE_LED_DRIVER_MAX695X
    src/max695x_led_test_simple.c
    src/stubs.c
    ${PLATFORM_EC}/driver/led/max695x.c
)
