# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lazor)

cros_ec_library_include_directories(include)

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC
  "src/power.c"
  "src/usb_pd_policy.c"
  "src/usbc_config.c")

zephyr_library_sources(
  "src/sku.c"
  "src/switchcap.c")

# Board specific implementation
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_I2C
  "src/i2c.c")
