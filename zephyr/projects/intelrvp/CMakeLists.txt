# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(intelrvp)

cros_ec_library_include_directories(include)
zephyr_library_sources("src/intel_rvp_board_id.c")

if(DEFINED CONFIG_BOARD_ADLRVP_NPCX)
  add_subdirectory(adlrvp)
  zephyr_library_sources("src/intelrvp.c")
endif()

if(DEFINED CONFIG_BOARD_MTLRVP_NPCX)
  add_subdirectory(mtlrvp)
  zephyr_library_sources("src/intelrvp.c")
endif()
