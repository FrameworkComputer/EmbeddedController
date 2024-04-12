# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(utils)

target_sources_ifdef(CONFIG_TEST_STATIC_IF app PRIVATE src/static_if.c)
target_sources_ifdef(CONFIG_TEST_UTILS app PRIVATE src/utils.c)
target_sources_ifdef(CONFIG_TEST_UTILS_STR app PRIVATE src/utils_str.c)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)
