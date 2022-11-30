# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(skyrim)

zephyr_include_directories("${PLATFORM_EC_PROGRAM_DIR}/skyrim/include")

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

target_sources(app PRIVATE src/${CONFIG_TEST_BOARD_NAME}/common.c)
