# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

zephyr_library_sources("src/common.c")
zephyr_library_sources("src/power_signals.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC
	"src/usb_pd_policy.c"
	"src/usbc_config.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_COMMON
	"src/led.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_AMD_STT
	"src/stt.c")

if(DEFINED CONFIG_BOARD_MORTHAL)
	project(morthal)
	zephyr_library_sources(
		"src/morthal/ppc_config.c"
		"src/morthal/usb_mux_config.c"
)
endif()

if(DEFINED CONFIG_BOARD_SKYRIM)
	project(skyrim)
	cros_ec_library_include_directories_ifdef(CONFIG_BOARD_SKYRIM include)
	zephyr_library_sources(
			"src/skyrim/usb_mux_config.c"
			"src/skyrim/ppc_config.c"
			"src/skyrim/form_factor.c"
			"src/skyrim/alt_charger.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "src/skyrim/fan.c")
endif()

if(DEFINED CONFIG_BOARD_WINTERHOLD)
	project(winterhold)
	zephyr_library_sources(
		"src/winterhold/usb_mux_config.c"
		"src/winterhold/ppc_config.c"
    )
endif()
