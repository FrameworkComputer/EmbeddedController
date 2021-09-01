# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(drivers)

# Include the local test directory for shimmed_test_tasks.h
zephyr_include_directories("${CMAKE_CURRENT_SOURCE_DIR}")
zephyr_include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")
zephyr_include_directories("${PLATFORM_EC}/driver/ppc/")

FILE(GLOB test_sources src/*.c)
target_sources(app PRIVATE ${test_sources})

target_sources(app PRIVATE "${PLATFORM_EC}/test/cbi.c")
