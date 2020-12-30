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

set(PLATFORM_EC_BOARD "${PLATFORM_EC}/board/volteer" CACHE PATH
    "Path to the platform/ec board directory")
set(PLATFORM_EC_BASEBOARD "${PLATFORM_EC}/baseboard/volteer" CACHE PATH
    "Path to the platform/ec baseboard directory")

# Include board specific header files
zephyr_include_directories(
  include
  ${PLATFORM_EC_BASEBOARD}
  ${PLATFORM_EC_BOARD})

# Include selected EC source for the baseboard
target_sources(app PRIVATE "${PLATFORM_EC_BASEBOARD}/battery_presence.c")
target_sources(app PRIVATE "${PLATFORM_EC_BASEBOARD}/charger.c")
target_sources(app PRIVATE "${PLATFORM_EC_BASEBOARD}/usb_pd_policy.c")

# Include selected EC source for the board
target_sources(app PRIVATE "${PLATFORM_EC_BOARD}/battery.c")
target_sources(app PRIVATE "${PLATFORM_EC_BOARD}/usbc_config.c")

# Local sources to make things build. These are being removed as we are able
# to build the EC source instead.
target_sources(app PRIVATE "src/pwrok_signals.c")
