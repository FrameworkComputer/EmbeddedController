# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Initialize Zephyr
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rauru_usbc_tests)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

zephyr_include_directories(
	"${PLATFORM_EC_PROGRAM_DIR}/rauru/include"
	include)

target_sources(app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/rauru/src/usbc.c)

# Add the test source
target_sources(app PRIVATE
	src/main.c
    src/usbc_test.c
)
