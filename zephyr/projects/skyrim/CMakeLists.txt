# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(guybrush)

zephyr_library_include_directories_ifdef(CONFIG_BOARD_SKYRIM include)
zephyr_library_include_directories_ifdef(CONFIG_BOARD_GUYBRUSH include_guybrush)

zephyr_library_sources_ifdef(CONFIG_BOARD_SKYRIM "power_signals.c")
zephyr_library_sources_ifdef(CONFIG_BOARD_GUYBRUSH "power_signals_guybrush.c")

zephyr_library_sources_ifdef(CONFIG_BOARD_SKYRIM "usbc_config.c")
zephyr_library_sources_ifdef(CONFIG_BOARD_GUYBRUSH "usbc_config_guybrush.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC
	"usb_pd_policy.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_COMMON
	"led.c")
