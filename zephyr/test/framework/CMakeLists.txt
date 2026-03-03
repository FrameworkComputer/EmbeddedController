# Copyright 2026 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(framework_tests)

# Set PLATFORM_EC to the EC root directory
set(PLATFORM_EC "${CMAKE_CURRENT_LIST_DIR}/../../..")

# Add test utilities
add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

# Framework program includes
target_include_directories(app PRIVATE
    ${PLATFORM_EC}/zephyr/program/framework/include
)

# Test sources
target_sources(app PRIVATE
    src/main.c
)

# Source under test
target_sources(app PRIVATE
    ${PLATFORM_EC}/zephyr/program/framework/src/uefi_app_mode.c
)
