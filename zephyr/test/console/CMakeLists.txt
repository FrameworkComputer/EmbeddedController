# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(console)

target_sources_ifdef(CONFIG_TEST_CONSOLE_OUTPUT app PRIVATE
  src/console_output.c)

target_sources_ifdef(CONFIG_TEST_CONSOLE_RESTRICTED app PRIVATE
  src/console_restricted.c)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)
