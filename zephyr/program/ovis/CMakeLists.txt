# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

zephyr_include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")
zephyr_library_sources_ifdef(CONFIG_AP_PWRSEQ "src/board_power.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/usbc_config.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/usb_pd_policy.c")

if(DEFINED CONFIG_BOARD_OVIS)
	project(ovis)
	add_subdirectory(ovis)
elseif(DEFINED CONFIG_BOARD_DEKU)
	project(deku)
	add_subdirectory(deku)
endif()
