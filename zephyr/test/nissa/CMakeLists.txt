# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(nissa)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

zephyr_include_directories("${PLATFORM_EC_PROGRAM_DIR}/nissa/include")

target_sources(app PRIVATE src/stubs.c)

target_sources_ifdef(
        CONFIG_TEST_NISSA_SUB_BOARD
        app PRIVATE
        src/sub_board.c
        src/log_module.c
        ${PLATFORM_EC_PROGRAM_DIR}/nissa/src/sub_board.c)

target_sources_ifdef(
        CONFIG_TEST_NISSA_COMMON
        app PRIVATE
        src/common.c
        ${PLATFORM_EC_PROGRAM_DIR}/nissa/src/common.c)

target_sources_ifdef(
        CONFIG_TEST_NISSA_NIVVIKS
        app PRIVATE
        src/nivviks.c
        ${PLATFORM_EC_PROGRAM_DIR}/nissa/nivviks/src/fan.c
        ${PLATFORM_EC_PROGRAM_DIR}/nissa/nivviks/src/form_factor.c
        ${PLATFORM_EC_PROGRAM_DIR}/nissa/nivviks/src/keyboard.c
        ${PLATFORM_EC_PROGRAM_DIR}/nissa/nivviks/src/led.c
)

if(CONFIG_TEST_NISSA_NIVVIKS_CHARGER)
    target_compile_definitions(app PRIVATE CHARGER_PRIMARY=0 CHARGER_SECONDARY=1)
    target_sources(
            app PRIVATE
            src/nivviks_charger.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/nivviks/src/usbc.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/nivviks/src/charger.c
    )
endif()

if(CONFIG_TEST_NISSA_NEREID)
    target_compile_definitions(app PRIVATE CHARGER_PRIMARY=0 CHARGER_SECONDARY=1)
    target_sources(
            app PRIVATE
            src/nereid.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/nereid/src/charger.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/nereid/src/hdmi.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/nereid/src/keyboard.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/nereid/src/usbc.c
    )
endif()

if(CONFIG_TEST_NISSA_GOTHRAX)
    target_compile_definitions(app PRIVATE CHARGER_PRIMARY=0 CHARGER_SECONDARY=1)
    target_sources(
            app PRIVATE
            src/gothrax.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/gothrax/src/charger.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/gothrax/src/hdmi.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/gothrax/src/keyboard.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/gothrax/src/usbc.c
    )
endif()
