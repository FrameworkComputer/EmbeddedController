# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_interface_library_named(test_utils)

zephyr_library_sources(fff.c)

if (NOT CONFIG_SHIMMED_TASKS AND NOT CONFIG_HAS_TEST_TASKS)
  zephyr_include_directories("${PLATFORM_EC}/include"
                             "${PLATFORM_EC}/fuzz"
                             "${PLATFORM_EC}/test"
                             "${PLATFORM_EC}/zephyr/shim/include")
  zephyr_library_sources(tasks_fakes.c)
endif()

# ztest_printk override only supports the native_sim board.
if (CONFIG_ZTEST_TC_UTIL_USER_OVERRIDE AND CONFIG_BOARD_NATIVE_SIM)
  zephyr_include_directories("${PLATFORM_EC}/zephyr/include/test")
  zephyr_library_sources(ztest_printk)
endif()

zephyr_library_link_libraries(test_utils)
