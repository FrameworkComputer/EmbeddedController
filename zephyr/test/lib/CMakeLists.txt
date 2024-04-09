# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(utils)

target_sources_ifdef(CONFIG_TEST_LIB_SBRK app PRIVATE src/sbrk.c)
target_sources_ifdef(CONFIG_TEST_LIB_STD_VECTOR app PRIVATE src/std_vector.cc)
target_sources_ifdef(CONFIG_TEST_LIB_STDLIB app PRIVATE src/stdlib.c)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)
