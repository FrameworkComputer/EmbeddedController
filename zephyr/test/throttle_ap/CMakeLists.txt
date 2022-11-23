# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(throttle_ap)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

zephyr_include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")

target_sources_ifdef(CONFIG_TEST_INTERRUPT app PRIVATE src/interrupt.c)
target_sources_ifdef(CONFIG_TEST_THROTTLE_AP app PRIVATE src/throttle_ap.c)
target_sources(app PRIVATE src/common.c)
