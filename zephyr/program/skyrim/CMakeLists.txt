# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

zephyr_include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")

zephyr_library_sources("src/common.c")
zephyr_library_sources("src/i2c_policy.c")
zephyr_library_sources("src/power_signals.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC
	"src/usb_pd_policy.c"
	"src/usb_mux_config.c"
	"src/usbc_config.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_COMMON
	"src/led.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_AMD_STT
	"src/stt.c")

if(DEFINED CONFIG_BOARD_SKYRIM)
	project(skyrim)
	add_subdirectory(skyrim)
endif()

if(DEFINED CONFIG_BOARD_WINTERHOLD)
	project(winterhold)
	add_subdirectory(winterhold)
endif()

if(DEFINED CONFIG_BOARD_FROSTFLOW)
	project(frostflow)
	add_subdirectory(frostflow)
endif()

if(DEFINED CONFIG_BOARD_CRYSTALDRIFT)
	project(crystaldrift)
	add_subdirectory(crystaldrift)
endif()

if(DEFINED CONFIG_BOARD_MARKARTH)
	project(markarth)
	add_subdirectory(markarth)
endif()
