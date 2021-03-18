# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

set(BOARD_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lazor)

zephyr_include_directories(include)

set(PLATFORM_EC_BOARD "${PLATFORM_EC}/board/lazor" CACHE PATH
  "Path to the platform/ec board directory")

zephyr_sources(
  "${PLATFORM_EC_BOARD}/sku.c"
  "${PLATFORM_EC_BOARD}/switchcap.c")
