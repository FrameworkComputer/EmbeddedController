# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Initialize Zephyr
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(skywalker_tests)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

zephyr_include_directories(
	"${PLATFORM_EC_PROGRAM_DIR}/skywalker/include"
	"${PLATFORM_EC_PROGRAM_DIR}/skywalker/skywalker/include"
	include)

target_sources(app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/skywalker/src/sensor.c
)

target_sources(app PRIVATE
	src/main.c
)

target_sources_ifdef(CONFIG_TEST_SKYWALKER app PRIVATE
	src/test_skywalker.c
)
