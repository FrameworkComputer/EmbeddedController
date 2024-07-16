# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(brox)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

add_subdirectory(tests/common)
add_subdirectory_ifdef(CONFIG_TEST_BOARD_BASEBOARD tests/baseboard)
add_subdirectory_ifdef(CONFIG_TEST_BOARD_LOTSO tests/lotso)
add_subdirectory_ifdef(CONFIG_TEST_BOARD_GREENBAYUPOC tests/greenbayupoc)
add_subdirectory_ifdef(CONFIG_TEST_BOARD_JUBILANT tests/jubilant)
