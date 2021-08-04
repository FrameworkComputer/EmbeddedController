# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lazor)

zephyr_library_include_directories(include)

set(PLATFORM_EC_BASEBOARD "${PLATFORM_EC}/baseboard/trogdor" CACHE PATH
    "Path to the platform/ec baseboard directory")
set(PLATFORM_EC_BOARD "${PLATFORM_EC}/board/lazor" CACHE PATH
  "Path to the platform/ec board directory")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC
  "${PLATFORM_EC_BASEBOARD}/hibernate.c"
  "${PLATFORM_EC_BASEBOARD}/power.c"
  "${PLATFORM_EC_BASEBOARD}/usbc_config.c"
  "${PLATFORM_EC_BASEBOARD}/usb_pd_policy.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_COMMON
  "${PLATFORM_EC_BOARD}/led.c")

zephyr_library_sources(
  "${PLATFORM_EC_BOARD}/battery.c"
  "${PLATFORM_EC_BOARD}/hibernate.c"
  "${PLATFORM_EC_BOARD}/sku.c"
  "${PLATFORM_EC_BOARD}/switchcap.c"
  "${PLATFORM_EC_BOARD}/usbc_config.c")

# Board specific implementation
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_I2C
  "src/i2c.c")
