# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(rex)

zephyr_include_directories("${PLATFORM_EC_PROGRAM_DIR}/rex/include")

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

add_subdirectory(tests/program)
add_subdirectory_ifdef(CONFIG_TEST_PROJECT_REX tests/rex)
add_subdirectory_ifdef(CONFIG_TEST_PROJECT_SCREEBO tests/screebo)
add_subdirectory_ifdef(CONFIG_TEST_PROJECT_KARIS tests/karis)
add_subdirectory_ifdef(CONFIG_TEST_PROJECT_REX_ISH tests/rex_ish)
