# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
add_compile_definitions(BOARD_KRABBY)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(krabby)

zephyr_library_include_directories(include)

set(PLATFORM_EC_BASEBOARD "${PLATFORM_EC}/baseboard/corsola" CACHE PATH
    "Path to the platform/ec baseboard directory")
set(PLATFORM_EC_BOARD "${PLATFORM_EC}/board/krabby" CACHE PATH
  "Path to the platform/ec board directory")

# Include selected EC source from the baseboard
zephyr_library_sources(
  "${PLATFORM_EC_BASEBOARD}/board_chipset.c"
  "${PLATFORM_EC_BASEBOARD}/board_id.c"
  "${PLATFORM_EC_BASEBOARD}/hibernate.c"
  "${PLATFORM_EC_BASEBOARD}/regulator.c"
  "${PLATFORM_EC_BASEBOARD}/usbc_config.c"
  "${PLATFORM_EC_BASEBOARD}/usb_pd_policy.c")

# Include selected EC source from the board
zephyr_library_sources(
  "${PLATFORM_EC_BOARD}/led.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_I2C
  "src/i2c.c")
