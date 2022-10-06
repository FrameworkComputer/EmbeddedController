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

zephyr_library_link_libraries(test_utils)
