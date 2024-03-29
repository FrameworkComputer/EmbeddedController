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
elseif(DEFINED CONFIG_BOARD_INTEL_ISH_5_6_0)
	project(brox-ish)
	add_subdirectory(brox-ish)
endif()
