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
