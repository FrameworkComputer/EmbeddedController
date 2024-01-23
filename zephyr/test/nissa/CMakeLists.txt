# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(nissa)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

zephyr_include_directories(
        "${PLATFORM_EC_PROGRAM_DIR}/nissa/include"
        include)

target_sources(app PRIVATE src/stubs.c)

if(CONFIG_TEST_NISSA_BOARD_POWER)
    # Test source
    target_sources(app PRIVATE src/board_power.c)
    # Module under test
    target_sources(app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/nissa/src/board_power.c)
endif()

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
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/nereid/src/usbc.c
    )
endif()

if(CONFIG_TEST_NISSA_NOKRIS)
    target_compile_definitions(app PRIVATE CHARGER_PRIMARY=0)
    target_sources(
            app PRIVATE
            src/nokris.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/nokris/src/charger.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/nokris/src/hdmi.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/nokris/src/usbc.c
    )
endif()

if(CONFIG_TEST_NISSA_GOTHRAX)
    target_compile_definitions(app PRIVATE CHARGER_PRIMARY=0 CHARGER_SECONDARY=1)
    target_sources(
            app PRIVATE
            src/gothrax.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/gothrax/src/charger.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/gothrax/src/hdmi.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/gothrax/src/usbc.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/gothrax/src/usb_mux_config.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/gothrax/src/form_factor.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/gothrax/src/fan.c
    )
endif()

if(CONFIG_TEST_NISSA_PUJJO)
    target_sources(
            app PRIVATE
            src/pujjo.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/pujjo/src/fan.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/pujjo/src/form_factor.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/pujjo/src/hdmi.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/pujjo/src/led.c
    )
endif()

if(CONFIG_TEST_NISSA_JOXER)
    target_compile_definitions(app PRIVATE CHARGER_PRIMARY=0 CHARGER_SECONDARY=1)
    target_sources(
            app PRIVATE
            src/joxer.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/joxer/src/charger.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/joxer/src/fan.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/joxer/src/form_factor.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/joxer/src/keyboard.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/joxer/src/usbc.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/joxer/src/sub_board.c
    )
endif()

if(CONFIG_TEST_NISSA_CRAASK)
    target_sources(
	    app PRIVATE
	    src/craask.c
	    ${PLATFORM_EC_PROGRAM_DIR}/nissa/craask/src/charger.c
	    ${PLATFORM_EC_PROGRAM_DIR}/nissa/craask/src/fan.c
	    ${PLATFORM_EC_PROGRAM_DIR}/nissa/craask/src/form_factor.c
	    ${PLATFORM_EC_PROGRAM_DIR}/nissa/craask/src/kb_backlight.c
	    ${PLATFORM_EC_PROGRAM_DIR}/nissa/craask/src/keyboard.c
	    ${PLATFORM_EC_PROGRAM_DIR}/nissa/craask/src/led.c
	    ${PLATFORM_EC_PROGRAM_DIR}/nissa/craask/src/thermal.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/craask/src/touch_panel.c
	    ${PLATFORM_EC_PROGRAM_DIR}/nissa/craask/src/usbc.c
    )
endif()

if(CONFIG_TEST_NISSA_CRAASKOV)
    zephyr_include_directories("${PLATFORM_EC_PROGRAM_DIR}/nissa/craaskov/include")
    target_sources(
	    app PRIVATE
	    src/craaskov.c
	    ${PLATFORM_EC_PROGRAM_DIR}/nissa/craaskov/src/charger.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/craaskov/src/hdmi.c
	    ${PLATFORM_EC_PROGRAM_DIR}/nissa/craaskov/src/keyboard.c
	    ${PLATFORM_EC_PROGRAM_DIR}/nissa/craaskov/src/usbc.c
    )
endif()

if(CONFIG_TEST_NISSA_PIRRHA)
    zephyr_include_directories("${PLATFORM_EC_PROGRAM_DIR}/nissa/pirrha/include")
    target_compile_definitions(app PRIVATE CHARGER_PRIMARY=0 CHARGER_SECONDARY=1)
    target_sources(
	    app PRIVATE
	    src/pirrha.c
	    ${PLATFORM_EC_PROGRAM_DIR}/nissa/pirrha/src/charger.c
	    ${PLATFORM_EC_PROGRAM_DIR}/nissa/pirrha/src/led.c
	    ${PLATFORM_EC_PROGRAM_DIR}/nissa/pirrha/src/usbc.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/pirrha/src/battery.c
            ${PLATFORM_EC_PROGRAM_DIR}/nissa/pirrha/src/board.c
    )
endif()

if(CONFIG_TEST_NISSA_ANRAGGAR)
    zephyr_include_directories("${PLATFORM_EC_PROGRAM_DIR}/nissa/anraggar/include")
    target_compile_definitions(app PRIVATE CHARGER_PRIMARY=0)
    target_sources(
            app PRIVATE
            src/anraggar.c
    )
endif()
