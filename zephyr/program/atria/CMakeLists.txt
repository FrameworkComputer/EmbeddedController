# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

if(DEFINED CONFIG_BOARD_ATRIARVP_RTK)
  project(atriarvp-rtk)
  zephyr_library_sources("src/rvp_board_power.c")
  zephyr_library_sources_ifdef(
    CONFIG_PDC_RUNTIME_PORT_CONFIG "src/rvp_usbc_runtime.c")
endif()

zephyr_include_directories("include/")

add_subdirectory(${PROJECT_NAME})
