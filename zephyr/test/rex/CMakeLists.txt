# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(rex)

zephyr_include_directories("${PLATFORM_EC_PROGRAM_DIR}/rex/include")

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

target_sources_ifdef(CONFIG_TEST_USB_PD_POLICY app PRIVATE src/usb_pd_policy.c)
target_sources_ifdef(CONFIG_TEST_USB_PD_POLICY app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/rex/src/usb_pd_policy.c)
