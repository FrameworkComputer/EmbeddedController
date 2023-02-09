# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(lotus)
cros_ec_library_include_directories(include)
zephyr_include_directories(include)
zephyr_library_sources(
	"src/battery.c"
	"src/board_host_command.c"
	"src/charger.c"
	"src/cypress_pd_common.c"
	"src/power_sequence.c"
	"src/adc.c"
)

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_COMMON
	"src/led_pwm.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_COMMON
	"src/led.c")

zephyr_library_sources_ifdef(CONFIG_HAS_TASK_POWERBTN_CUSTOMER
	"src/power_button_x86.c")
