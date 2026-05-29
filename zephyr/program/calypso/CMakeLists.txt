# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

if(DEFINED CONFIG_BOARD_MENSA)
	project(mensa)
endif()

if(DEFINED CONFIG_BOARD_C1NV)
	project(c1nv)
endif()

zephyr_library_sources("src/battery.c")
zephyr_library_sources_ifdef(CONFIG_AP_ARM_QUALCOMM_QC_EXP
				"src/switchcap.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_PMIC_PASSTHRU_POWER_SIGNALS
				"src/passthru_signal.c")

add_subdirectory(${PROJECT_NAME})
