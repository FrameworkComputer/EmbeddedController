# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

zephyr_include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")

if(DEFINED CONFIG_BOARD_RAURU)
	project(rauru)
	add_subdirectory(rauru)
endif()
