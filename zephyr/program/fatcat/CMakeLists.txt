# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

if(DEFINED CONFIG_BOARD_FATCAT_NPCX9M7F)
	project(fatcat_npcx9m7f)
	add_subdirectory(fatcat_npcx9m7f)
endif()

if(DEFINED CONFIG_BOARD_FATCAT_IT82002AW)
	project(fatcat_it82002aw)
	add_subdirectory(fatcat_it82002aw)
endif()

if(DEFINED CONFIG_BOARD_FRANCKA)
	project(francka)
	add_subdirectory(francka)
	zephyr_include_directories("francka/include")
	zephyr_library_sources(
			"francka/src/touch_panel.c"
			"francka/src/board_power.c"
			"francka/src/alt_charger.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_CUSTOMIZATION
	"francka/src/keyboard_customization.c"
	)
endif()

if(DEFINED CONFIG_BOARD_FELINO)
	project(felino)
	add_subdirectory(felino)
	zephyr_include_directories("felino/include")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_CUSTOMIZATION
					"felino/src/keyboard_customization.c")
endif()
