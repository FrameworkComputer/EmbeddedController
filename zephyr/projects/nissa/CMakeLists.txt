# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

zephyr_include_directories(include)
zephyr_library_sources("src/common.c")
zephyr_library_sources("src/sub_board.c")
zephyr_library_sources_ifdef(CONFIG_AP_PWRSEQ "src/board_power.c")

if(DEFINED CONFIG_BOARD_NIVVIKS)
	project(nivviks)
	zephyr_library_sources(
			"src/led.c"
			"nivviks/src/form_factor.c"
			"nivviks/src/keyboard.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "nivviks/src/fan.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "nivviks/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "nivviks/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_NEREID)
	project(nereid)
	zephyr_library_sources(
			"src/led.c"
			"nereid/src/keyboard.c"
			"nereid/src/hdmi.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "nereid/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "nereid/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_CRAASK)
	zephyr_library_sources(
			"craask/src/form_factor.c"
			"craask/src/keyboard.c"
			"craask/src/led.c"
	)
	project(craask)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "craask/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "craask/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_PUJJO)
	project(pujjo)
	zephyr_library_sources(
			"pujjo/src/led.c"
			"pujjo/src/keyboard.c"
			"pujjo/src/hdmi.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "pujjo/src/fan.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "pujjo/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "pujjo/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_XIVU)
        project(xivu)
        zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "xivu/src/usbc.c")
        zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "xivu/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_JOXER)
	project(joxer)
	zephyr_library_sources(
			"joxer/src/led.c"
			"joxer/src/keyboard.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "joxer/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "joxer/src/charger.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "joxer/src/fan.c")
endif()
