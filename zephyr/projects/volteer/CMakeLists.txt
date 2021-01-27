# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)
set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR})
set(BOARD volteer)

# Append ${PLATFORM_EC}/zephyr/include/drivers to SYSCALL_INCLUDE_DIRS
# variable for generating __syscall api functions by the
# `gen_syscalls.py` script.
# TODO: remove this when zmake support
list(APPEND
     SYSCALL_INCLUDE_DIRS
     "${CMAKE_CURRENT_LIST_DIR}/../../include/drivers"
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
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_BATTERY
  "${PLATFORM_EC_BASEBOARD}/battery_presence.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_CHARGE_MANAGER
  "${PLATFORM_EC_BASEBOARD}/charger.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_CBI
  "${PLATFORM_EC_BASEBOARD}/cbi.c"
  "${PLATFORM_EC_BASEBOARD}/cbi_ec_fw_config.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ
  "${PLATFORM_EC_BASEBOARD}/power.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_POWER_DELIVERY
  "${PLATFORM_EC_BASEBOARD}/usb_pd_policy.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USBC
  "${PLATFORM_EC_BASEBOARD}/usbc_config.c")

# Include selected EC source for the board
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_BATTERY
  "${PLATFORM_EC_BOARD}/battery.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_CBI
  "${PLATFORM_EC_BOARD}/cbi.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_LED_COMMON
  "${PLATFORM_EC_BOARD}/led.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USBC
  "${PLATFORM_EC_BOARD}/usbc_config.c")
