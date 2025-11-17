# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

if(DEFINED CONFIG_BOARD_OCELOTRVP_ITE)
	project(ocelotrvp-ite)
	zephyr_library_sources("src/rvp_board_id.c")
	zephyr_library_sources("src/rvp_board_power.c")
	zephyr_library_sources("src/rvp_usbc_runtime.c")
endif()

if(DEFINED CONFIG_BOARD_OCELOTRVP_NPCX)
	project(ocelotrvp-npcx)
	zephyr_library_sources("src/rvp_board_id.c")
	zephyr_library_sources("src/rvp_board_power.c")
	zephyr_library_sources("src/rvp_usbc_runtime.c")
endif()

if(DEFINED CONFIG_BOARD_OCELOTRVP_MCHP)
	project(ocelotrvp-mchp)
	zephyr_library_sources("src/rvp_board_power.c")
	zephyr_library_sources("src/rvp_usbc_runtime.c")
endif()

if(DEFINED CONFIG_BOARD_OJAL)
	project(ojal)
	zephyr_library_sources("ojal/src/board_power.c")
endif()

if(DEFINED CONFIG_BOARD_KODKOD)
	project(kodkod)
	zephyr_library_sources("kodkod/src/board_power.c")
	zephyr_library_sources("kodkod/src/keyboard_customization.c")
	zephyr_library_sources("kodkod/src/suspend_resume.c")
endif()

if(DEFINED CONFIG_BOARD_MATSU)
	project(matsu)
	zephyr_library_sources("matsu/src/board_power.c")
	zephyr_library_sources("matsu/src/suspend_resume.c")
	zephyr_library_sources("matsu/src/touch_panel.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_CUSTOMIZATION
					"matsu/src/keyboard_customization.c")
endif()

if(DEFINED CONFIG_BOARD_OCICAT)
	project(ocicat)
	zephyr_library_sources("ocicat/src/keyboard_customization.c")
	zephyr_library_sources("ocicat/src/board_power.c")
	zephyr_library_sources("ocicat/src/fan.c")
	zephyr_library_sources("ocicat/src/board.c")
	zephyr_library_sources("ocicat/src/thermal.c")
endif()

if(DEFINED CONFIG_BOARD_OCELOTRVP_RTK)
	project(ocelotrvp-rtk)
	zephyr_library_sources("src/rvp_board_power.c")
	zephyr_library_sources("src/rvp_usbc_runtime.c")
endif()

zephyr_include_directories("include/")

zephyr_include_directories_ifdef(CONFIG_PLATFORM_EC_DEDICATED_CHARGE_PORT
  "../intelrvp/include")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_DEDICATED_CHARGE_PORT
  "../intelrvp/src/dc_jack.c")

if(DEFINED CONFIG_BOARD_INTEL_ISH_5_8_0)
	project(ocelot-ish)
endif()

add_subdirectory(${PROJECT_NAME})
