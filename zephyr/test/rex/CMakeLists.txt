# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(rex)

zephyr_include_directories("${PLATFORM_EC_PROGRAM_DIR}/rex/include")
zephyr_include_directories("${PLATFORM_EC_PROGRAM_DIR}/rex/rex/include")
zephyr_include_directories("${PLATFORM_EC_PROGRAM_DIR}/rex/screebo/include")

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

target_sources_ifdef(CONFIG_TEST_USB_PD_POLICY app PRIVATE src/usb_pd_policy.c)
target_sources_ifdef(CONFIG_TEST_USB_PD_POLICY app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/rex/src/usb_pd_policy.c)

target_sources_ifdef(CONFIG_TEST_BOARD_POWER app PRIVATE src/board_power.c)
target_sources_ifdef(CONFIG_TEST_BOARD_POWER app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/rex/src/board_power.c)

target_sources_ifdef(CONFIG_TEST_BATTERY_PRESENT app PRIVATE src/battery_present.c)
target_sources_ifdef(CONFIG_TEST_BATTERY_PRESENT app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/rex/src/usb_pd_policy.c)

target_sources_ifdef(CONFIG_TEST_BOARD_USB_MUX_CONFIG app PRIVATE src/usb_mux_config.c)
target_sources_ifdef(CONFIG_TEST_BOARD_USB_MUX_CONFIG app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/rex/rex/src/usb_mux_config.c)

target_sources_ifdef(CONFIG_TEST_SCREEBO_USB_CONFIG app PRIVATE src/screebo_usb_config.c)
target_sources_ifdef(CONFIG_TEST_SCREEBO_USB_CONFIG app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/rex/screebo/src/usb_config.c)
