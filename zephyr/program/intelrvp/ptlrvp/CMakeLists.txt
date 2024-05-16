# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(ptlrvp_mchp)

zephyr_include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")

zephyr_library_sources("src/board_id.c")
zephyr_library_sources("src/board_kbd.c")
zephyr_library_sources("src/board_power.c")
