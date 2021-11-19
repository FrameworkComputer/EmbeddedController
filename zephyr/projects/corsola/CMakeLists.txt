# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

zephyr_library_include_directories(include)

# Include selected EC source from the baseboard
zephyr_library_sources(
	"src/board_chipset.c"
	"src/hibernate.c"
	"src/usbc_config.c"
	"src/usb_pd_policy.c"
)

if(DEFINED CONFIG_BOARD_KRABBY)
	project(krabby)
	zephyr_library_sources("src/krabby/hooks.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_I2C "src/krabby/i2c.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_COMMON
				     "src/krabby/led.c")
endif()
