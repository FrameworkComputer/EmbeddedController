# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

zephyr_library_sources(
		"src/fakes.c"
		"src/one_wire.c"
)

zephyr_include_directories(
		"include"
)

if(DEFINED CONFIG_BOARD_ROACH)
	project(roach)
elseif(DEFINED CONFIG_BOARD_AXII)
	project(axii)
	add_subdirectory(axii)
elseif(DEFINED CONFIG_BOARD_KELPIE)
	project(kelpie)
endif()
