# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

if(DEFINED CONFIG_BOARD_BLUEY)
	project(bluey)
	add_subdirectory(bluey)
endif()

if(DEFINED CONFIG_BOARD_QUENBI)
	project(quenbi)
	add_subdirectory(quenbi)
endif()

if(DEFINED CONFIG_BOARD_QUARTZ)
	project(quartz)
	add_subdirectory(quartz)
endif()

zephyr_library_sources("src/battery.c")
zephyr_library_sources("src/switchcap.c")
zephyr_library_sources("src/chipset.c")
zephyr_library_sources("src/adsp_i2c_mode.c")
