# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

zephyr_library_sources("common.c")
zephyr_library_sources("power_signals.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC
	"usb_pd_policy.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_COMMON
	"led.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_AMD_STT
	"stt.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "fan.c")

if(DEFINED CONFIG_BOARD_MORTHAL)
	project(morthal)
	zephyr_library_sources(
		"usbc_config.c"
)
endif()

if(DEFINED CONFIG_BOARD_SKYRIM)
	project(skyrim)
	cros_ec_library_include_directories_ifdef(CONFIG_BOARD_SKYRIM include)
	zephyr_library_sources(
			"src/skyrim/form_factor.c"
			"usbc_config.c"
	)
endif()

if(DEFINED CONFIG_BOARD_WINTERHOLD)
	project(winterhold)
	zephyr_library_sources(
    		"winterhold/src/usbc_config.c"
    )
endif()
