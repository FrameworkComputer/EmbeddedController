# Copyright 2022 The ChromiumOS Authors.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rex)

zephyr_include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")
zephyr_library_sources_ifdef(CONFIG_AP_PWRSEQ "src/board_power.c")

