# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
cmake_minimum_required(VERSION 3.20.1)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

zephyr_library_sources_ifdef(CONFIG_AP_PWRSEQ "src/board_power.c")

if(DEFINED CONFIG_BOARD_BROX)
	project(brox)
	add_subdirectory(brox)

	zephyr_library_sources("src/hibernate.c")
	zephyr_library_sources("src/suspend_resume.c")
	zephyr_library_sources("src/usbc.c")
elseif(DEFINED CONFIG_BOARD_GREENBAYUPOC)
	project(greenbayupoc)
	add_subdirectory(greenbayupoc)

	zephyr_library_sources("src/hibernate.c")
	zephyr_library_sources("src/suspend_resume.c")
	zephyr_library_sources("src/usbc.c")
elseif(DEFINED CONFIG_BOARD_JUBILANT)
	project(jubilant)
	add_subdirectory(jubilant)

	zephyr_include_directories("jubilant/include")
	zephyr_library_sources("src/hibernate.c")
	zephyr_library_sources("src/suspend_resume.c")
	zephyr_library_sources("src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_CUSTOMIZATION
					"jubilant/src/keyboard_customization.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CUSTOM_FAN_CONTROL
					"jubilant/src/thermal.c")
elseif(DEFINED CONFIG_BOARD_LOTSO)
	project(lotso)
	add_subdirectory(lotso)

	zephyr_include_directories("lotso/include")
	zephyr_library_sources("src/hibernate.c")
	zephyr_library_sources("src/suspend_resume.c")
	zephyr_library_sources("src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_CUSTOMIZATION
					"lotso/src/keyboard_customization.c")
elseif(DEFINED CONFIG_BOARD_INTEL_ISH_5_6_0)
	project(brox-ish)
	add_subdirectory(brox-ish)
endif()
