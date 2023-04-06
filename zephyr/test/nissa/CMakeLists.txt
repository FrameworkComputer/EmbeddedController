# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(nissa)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

zephyr_include_directories("${PLATFORM_EC_PROGRAM_DIR}/nissa/include")

target_sources(app PRIVATE src/stubs.c)

target_sources_ifdef(
        CONFIG_TEST_NISSA_SUB_BOARD
        app PRIVATE
        src/sub_board.c
        src/log_module.c
        ${PLATFORM_EC_PROGRAM_DIR}/nissa/src/sub_board.c)

target_sources_ifdef(
        CONFIG_TEST_NISSA_COMMON
        app PRIVATE
        src/common.c
        ${PLATFORM_EC_PROGRAM_DIR}/nissa/src/common.c)
