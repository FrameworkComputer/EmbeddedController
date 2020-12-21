# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)
set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR})
set(BOARD volteer)


# Append {ZEPHYR_CHROME}/include/drivers to SYSCALL_INCLUDE_DIRS variable for
# generating __syscall api functions by the `gen_syscalls.py` script.
# TODO: remove this when zmake support
list(APPEND
    SYSCALL_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/../../../include/drivers
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(volteer)

# Include board specific header files
zephyr_include_directories(include)
target_sources(app PRIVATE "src/battery_presence.c")
target_sources(app PRIVATE "src/pwrok_signals.c")
