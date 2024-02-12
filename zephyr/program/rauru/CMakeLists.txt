# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

zephyr_include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")
target_sources(app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/rauru/src/usbc.c
			${PLATFORM_EC_PROGRAM_DIR}/rauru/src/hibernate.c)
target_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_DP_MODE app PRIVATE
		${PLATFORM_EC_PROGRAM_DIR}/rauru/src/dp_alt_mode.c)
target_sources_ifdef(CONFIG_RAURU_BOARD_HAS_HDMI_SUPPORT app PRIVATE
		${PLATFORM_EC_PROGRAM_DIR}/rauru/src/hdmi.c)

if(DEFINED CONFIG_BOARD_RAURU)
	project(rauru)
	add_subdirectory(rauru)
endif()
