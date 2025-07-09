# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(npcx_monitor)

set(CMAKE_EXECUTABLE_SUFFIX ".elf")

zephyr_include_directories(include)
add_subdirectory(${PLATFORM_EC}/zephyr/shim/chip/npcx/npcx_monitor monitor_bin)
