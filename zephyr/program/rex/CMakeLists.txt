# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

zephyr_include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")
zephyr_library_sources_ifdef(CONFIG_AP_PWRSEQ "src/board_power.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_BATTERY "src/battery_present.c")
zephyr_library_sources_ifdef(CONFIG_BOARD_REX_USBC_CONFIG "src/usbc_config.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/usb_pd_policy.c")

if(DEFINED CONFIG_BOARD_REX)
	project(rex)
	add_subdirectory(rex)
elseif(DEFINED CONFIG_BOARD_SCREEBO)
	project(screebo)
	add_subdirectory(screebo)
elseif(DEFINED CONFIG_BOARD_KARIS)
	project(karis)
	add_subdirectory(karis)
elseif(DEFINED CONFIG_BOARD_INTEL_ISH_5_6_0)
	project(rex-ish)
	add_subdirectory(rex-ish)
endif()
