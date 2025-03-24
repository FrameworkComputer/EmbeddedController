# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

if(DEFINED CONFIG_BOARD_OCELOT_ITE)
	project(ocelot_ite)
endif()

if(DEFINED CONFIG_BOARD_OCELOT_NUVOTON)
	project(ocelot_nuvoton)
endif()

if(DEFINED CONFIG_BOARD_OCELOT_MICROCHIP)
	project(ocelot_microchip)
endif()

add_subdirectory(${PROJECT_NAME})
