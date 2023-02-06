# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(skyrim)

zephyr_include_directories("${PLATFORM_EC_PROGRAM_DIR}/skyrim/include")

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

target_sources(app PRIVATE src/common.c src/${CONFIG_TEST_BOARD_NAME}/common.c)

target_sources_ifdef(CONFIG_TEST_BOARD_FAN app PRIVATE src/${CONFIG_TEST_BOARD_FAN_SRC} ${PLATFORM_EC_PROGRAM_DIR}/skyrim/${CONFIG_TEST_BOARD_NAME}/src/fan.c)
target_sources_ifdef(CONFIG_TEST_BOARD_PPC_CONFIG app PRIVATE src/${CONFIG_TEST_BOARD_PPC_CONFIG_SRC} ${PLATFORM_EC_PROGRAM_DIR}/skyrim/${CONFIG_TEST_BOARD_NAME}/src/ppc_config.c)
target_sources_ifdef(CONFIG_TEST_BOARD_USB_PD_POLICY app PRIVATE src/baseboard/usb_pd_policy.c ${PLATFORM_EC_PROGRAM_DIR}/skyrim/src/usb_pd_policy.c)
target_sources_ifdef(CONFIG_TEST_BOARD_USB_MUX_CONFIG app PRIVATE src/${TEST_BOARD_USB_MUX_CONFIG_SRC} ${PLATFORM_EC_PROGRAM_DIR}/skyrim/${CONFIG_TEST_BOARD_NAME}/src/usb_mux_config.c)
