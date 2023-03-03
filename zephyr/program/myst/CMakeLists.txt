# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")


zephyr_library_sources("src/common.c")
zephyr_library_sources("src/power_signals.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC
	"src/usb_pd_policy.c"
	"src/usbc_config.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_COMMON "src/led.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_AMD_STT "src/stt.c")

if(DEFINED CONFIG_BOARD_MYST)
	project(myst)
	add_subdirectory(myst)
endif()
