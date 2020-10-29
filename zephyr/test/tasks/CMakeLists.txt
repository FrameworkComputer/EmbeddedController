# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)
set(BOARD native_posix)
project(tasks)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# Need to ensure that we are including only zephyr definitions in include files
# We cannot set these via kconfig, since this unit test does not bring in the
# zephyr-chrome repository
zephyr_compile_definitions("CONFIG_ZEPHYR")
zephyr_compile_definitions("CONFIG_SHIMMED_TASKS")

# We need to include the EC include directory and this local test directory
# for the task defines
zephyr_include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${PLATFORM_EC}/zephyr/shim/include"
  "${PLATFORM_EC}/fuzz"
  "${PLATFORM_EC}/test"
  "${PLATFORM_EC}/include")

# Include the test source and the file under test
target_sources(app PRIVATE main.c)
target_sources(app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../shim/src/tasks.c")
