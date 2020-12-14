# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)
set(BOARD native_posix)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tasks)

# We need to include the EC include directory and this local test directory
# for the task defines
zephyr_include_directories("${PLATFORM_EC}/include")

# Include the test source and the file under test
target_sources(app PRIVATE main.c)
target_sources(app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../shim/src/crc.c")
