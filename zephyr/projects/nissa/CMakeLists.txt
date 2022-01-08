# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

zephyr_include_directories(include)
zephyr_library_sources("src/sub_board.c")

if(DEFINED CONFIG_BOARD_NIVVIKS)
	project(nivviks)
	zephyr_library_sources("src/nivviks/board_config.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_NEREID)
	project(nereid)
endif()
