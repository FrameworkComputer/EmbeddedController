# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_include_directories("${PLATFORM_EC_PROGRAM_DIR}/brox/juchi/include")
zephyr_include_directories("${PLATFORM_EC}/zephyr/shim/chip/it8xxx2/include")

target_sources_ifdef(CONFIG_TEST_BOARD_JUCHI_KEYBOARD app PRIVATE src/keyboard.c)
target_sources_ifdef(CONFIG_TEST_BOARD_JUCHI_KEYBOARD app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/brox/juchi/src/keyboard.c
	${PLATFORM_EC_PROGRAM_DIR}/brox/juchi/src/keyboard_customization.c)

target_sources_ifdef(CONFIG_TEST_BOARD_JUCHI_FAN app PRIVATE src/fan.c)
target_sources_ifdef(CONFIG_TEST_BOARD_JUCHI_FAN app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/brox/juchi/src/thermal.c)

target_sources_ifdef(CONFIG_TEST_BOARD_JUCHI_TOUCH_PANEL app PRIVATE src/touch_panel.c)
target_sources_ifdef(CONFIG_TEST_BOARD_JUCHI_TOUCH_PANEL app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/brox/juchi/src/touch_panel.c)
target_sources_ifdef(CONFIG_TEST_BOARD_LOTSO_SENSOR app PRIVATE src/sensor.c)
target_sources_ifdef(CONFIG_TEST_BOARD_JUCHI_SENSOR app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/brox/juchi/src/board.c)

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
