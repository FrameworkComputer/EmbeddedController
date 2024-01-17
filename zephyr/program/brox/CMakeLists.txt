# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
cmake_minimum_required(VERSION 3.20.1)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

zephyr_library_sources_ifdef(CONFIG_AP_PWRSEQ "src/board_power.c")
zephyr_library_sources_ifdef(CONFIG_AP_PWRSEQ "src/charger.c")
zephyr_library_sources("src/hibernate.c")

if(DEFINED CONFIG_BOARD_BROX)
	project(brox)
	add_subdirectory(brox)
endif()
