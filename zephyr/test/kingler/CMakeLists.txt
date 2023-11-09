# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(kingler)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

zephyr_include_directories("${PLATFORM_EC_PROGRAM_DIR}/corsola/include")
zephyr_include_directories("${PLATFORM_EC_PROGRAM_DIR}/../test/drivers/common/include")

target_sources(app PRIVATE src/fakes.c)

target_sources_ifdef(
        CONFIG_TEST_KINGLER_USBC
        app
        PRIVATE
        ${PLATFORM_EC_PROGRAM_DIR}/corsola/src/npcx_usbc.c
        ${PLATFORM_EC_PROGRAM_DIR}/corsola/src/npcx_usb_pd_policy.c
        ${PLATFORM_EC_PROGRAM_DIR}/corsola/src/usb_pd_policy.c
        ${PLATFORM_EC_PROGRAM_DIR}/corsola/src/usbc.c
        src/kingler_usbc.c)

target_sources_ifdef(CONFIG_TEST_CORSOLA_USBC app PRIVATE
                     ${PLATFORM_EC_PROGRAM_DIR}/corsola/src/usbc.c
                     src/corsola_usbc.c)

target_sources_ifdef(CONFIG_TEST_STEELIX_RUSTY app PRIVATE
                     ${PLATFORM_EC_PROGRAM_DIR}/corsola/steelix/src/board.c)
target_sources_ifdef(CONFIG_TEST_FORM_FACTOR_CONVERTIBLE app PRIVATE
                     src/tablet.c)
target_sources_ifdef(CONFIG_TEST_FORM_FACTOR_CLAMSHELL app PRIVATE
                     src/clamshell.c)
target_sources_ifdef(CONFIG_TEST_STEELIX_LED app PRIVATE
		     ${PLATFORM_EC_PROGRAM_DIR}/corsola/steelix/src/led.c
                     src/steelix_led.c)
target_sources_ifdef(
        CONFIG_TEST_DB_DETECT_TYPEC app PRIVATE
        ${PLATFORM_EC_PROGRAM_DIR}/corsola/src/variant_db_detection.c
        src/db_detect_typec.c)
target_sources_ifdef(
        CONFIG_TEST_DB_DETECT_HDMI
        app
        PRIVATE
        src/db_detect_hdmi.c
        ${PLATFORM_EC_PROGRAM_DIR}/corsola/src/variant_db_detection.c
        ${PLATFORM_EC}/zephyr/shim/src/usb_muxes.c
        ${PLATFORM_EC}/driver/usb_mux/usb_mux.c
        ${PLATFORM_EC}/driver/usb_mux/virtual.c)
target_sources_ifdef(
        CONFIG_TEST_DB_DETECT_NONE app PRIVATE
        ${PLATFORM_EC_PROGRAM_DIR}/corsola/src/variant_db_detection.c
        src/db_detect_none.c)
target_sources_ifdef(CONFIG_TEST_ALT_SENSOR_PROBE app PRIVATE src/alt_sensor.c)
target_sources_ifdef(CONFIG_TEST_KINGLER_CCD app PRIVATE src/ccd.c
                     ${PLATFORM_EC_PROGRAM_DIR}/corsola/src/board.c)
target_sources_ifdef(CONFIG_TEST_VOLTORB app PRIVATE src/voltorb_usbc.c
                     ${PLATFORM_EC_PROGRAM_DIR}/corsola/voltorb/src/usbc.c)
target_sources_ifdef(
        CONFIG_TEST_DB_DETECTION_USB_COUNT
        app
        PRIVATE
        ${PLATFORM_EC_PROGRAM_DIR}/corsola/src/usbc.c
        src/usb_port_count.c)

target_sources_ifdef(
        CONFIG_TEST_PONYTA
        app
        PRIVATE
        ${PLATFORM_EC_PROGRAM_DIR}/corsola/ponyta/src/board.c
        src/ponyta_board.c)
