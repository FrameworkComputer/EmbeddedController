# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(roach)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

zephyr_include_directories(
	include)

target_sources(app PRIVATE src/main.c)

target_sources_ifdef(CONFIG_TEST_AXII app PRIVATE
		src/axii.c
		"${PLATFORM_EC_PROGRAM_DIR}/roach/axii/src/main.c")
