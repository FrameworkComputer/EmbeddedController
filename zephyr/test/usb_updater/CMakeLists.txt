# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(usb_updater)

zephyr_include_directories(include)

target_sources(app PRIVATE
	src/fakes.c
	src/update_fw_test.c
	src/usb_update_test.c
	src/vendor_command_test.c
)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)
