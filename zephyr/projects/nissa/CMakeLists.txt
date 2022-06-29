# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

zephyr_include_directories(include)
zephyr_library_sources("src/common.c")
zephyr_library_sources_ifdef(CONFIG_AP_PWRSEQ "src/board_power.c")

if(DEFINED CONFIG_BOARD_NIVVIKS)
	project(nivviks)
	zephyr_library_sources(
			"src/led.c"
			"src/nivviks/form_factor.c"
			"src/nivviks/keyboard.c"
	)
	zephyr_library_sources("src/sub_board.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "src/nivviks/fan.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/nivviks/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/nivviks/charger.c")
endif()
if(DEFINED CONFIG_BOARD_NEREID)
	project(nereid)
	zephyr_library_sources(
			"src/led.c"
			"src/nereid/keyboard.c"
	)
	zephyr_library_sources("src/sub_board.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/nereid/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/nereid/charger.c")
endif()
if(DEFINED CONFIG_BOARD_CRAASK)
	zephyr_library_sources(
			"src/craask/led.c"
	)
	project(craask)
	zephyr_library_sources("src/sub_board.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/craask/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/craask/charger.c")
endif()
if(DEFINED CONFIG_BOARD_PUJJO)
	project(pujjo)
	zephyr_library_sources(
			"src/led.c"
			"src/pujjo/keyboard.c"
	)
	zephyr_library_sources("src/sub_board.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "src/pujjo/fan.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/pujjo/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/pujjo/charger.c")
endif()
if(DEFINED CONFIG_BOARD_XIVU)
        project(xivu)
        zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/xivu/usbc.c")
        zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/xivu/charger.c")
endif()
if(DEFINED CONFIG_BOARD_JOXER)
	project(joxer)
	zephyr_library_sources(
			"src/led.c"
			"src/joxer/keyboard.c"
	)
	zephyr_library_sources("src/sub_board.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/joxer/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/joxer/charger.c")
endif()
