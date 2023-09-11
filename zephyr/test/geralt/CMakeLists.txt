# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(geralt)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

zephyr_include_directories(
	"${PLATFORM_EC_PROGRAM_DIR}/geralt/include"
	include)

target_sources(app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/geralt/src/usbc_config.c
	${PLATFORM_EC_PROGRAM_DIR}/geralt/src/usb_pd_policy.c)

target_sources_ifdef(CONFIG_TEST_GERALT
	app PRIVATE
	src/main.c
	src/common.c)
