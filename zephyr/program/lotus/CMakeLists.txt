# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

zephyr_library_sources("src/adc.c")
zephyr_library_sources("src/board_host_command.c")
zephyr_library_sources("src/cypress_pd_common.c")
zephyr_library_sources("src/flash_storage.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_COMMON
	"src/led_pwm.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_COMMON
	"src/led.c")
zephyr_library_sources_ifdef(CONFIG_HAS_TASK_POWERBTN_CUSTOMER
	"src/power_button_x86.c")

if(DEFINED CONFIG_BOARD_LOTUS)
	project(lotus)
	cros_ec_library_include_directories_ifdef(CONFIG_BOARD_LOTUS include)
	zephyr_include_directories("include/lotus")
	zephyr_library_sources(
			"lotus/src/battery.c"
			"lotus/src/charger.c"
			"lotus/src/input_module.c"
			"lotus/src/gpu.c"
			"lotus/src/power_sequence.c"
			"lotus/src/f75303.c"
	)
endif()
if(DEFINED CONFIG_BOARD_AZALEA)
	project(azalea)
	cros_ec_library_include_directories_ifdef(CONFIG_BOARD_AZALEA include)
	zephyr_include_directories("include/azalea")
	zephyr_library_sources(
			"azalea/src/battery.c"
			"azalea/src/charger.c"
			"azalea/src/power_sequence.c"
	)
endif()
