# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

cros_ec_library_include_directories(include)

# Include selected EC source from the baseboard
zephyr_library_sources(
	"src/ccd_mode.c"
	"src/backlight.c"
	"src/hibernate.c"
	"src/hooks.c"
)

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_I2C "src/i2c.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC
			"src/dp_alt_mode.c"
			"src/usbc_config.c"
			"src/usb_pd_policy.c"
)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_DETACHABLE_BASE
			"src/base_detect.c"
)

zephyr_include_directories(
			"include"
			"../roach/include"
)

if(DEFINED CONFIG_BOARD_GERALT)
	project(geralt)
endif()
