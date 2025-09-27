# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

target_sources(app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/tanjiro/src/dp_alt_mode.c
	${PLATFORM_EC_PROGRAM_DIR}/tanjiro/src/hibernate.c
	${PLATFORM_EC_PROGRAM_DIR}/tanjiro/src/hooks.c
	${PLATFORM_EC_PROGRAM_DIR}/tanjiro/src/i2c.c)

if(DEFINED CONFIG_BOARD_TANJIRO)
	project(tanjiro)

elseif(DEFINED CONFIG_BOARD_SAPPHIRE)
	project(sapphire)
	add_subdirectory(sapphire)
endif()
