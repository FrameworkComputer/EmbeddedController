# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

if(DEFINED CONFIG_BOARD_ATRIARVP_RTK)
  project(atriarvp-rtk)
endif()

zephyr_include_directories("include/")

add_subdirectory(${PROJECT_NAME})
