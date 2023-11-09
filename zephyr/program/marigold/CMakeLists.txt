# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

zephyr_library_sources("src/adc.c")
zephyr_library_sources("src/board_function.c")
zephyr_library_sources("src/board_host_command.c")
zephyr_library_sources("src/cypress_pd_common.c")
zephyr_library_sources("src/flash_storage.c")
zephyr_library_sources("src/factory.c")
zephyr_library_sources("src/uefi_app_mode.c")
zephyr_library_sources("src/diagnostics.c")
zephyr_library_sources("src/ucsi.c")
zephyr_library_sources("src/als.c")
zephyr_library_sources("src/hid_device.c")
zephyr_library_sources("src/common_cpu_power.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_COMMON
	"src/led_pwm.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_COMMON
	"src/led.c")

if(DEFINED CONFIG_BOARD_MARIGOLD)
	project(marigold)
	cros_ec_library_include_directories_ifdef(CONFIG_BOARD_MARIGOLD include)
	zephyr_include_directories("include/marigold")
	zephyr_library_sources(
			"marigold/src/battery.c"
			"marigold/src/charger.c"
			"marigold/src/cpu_power.c"
			"marigold/src/power_sequence.c"
			"marigold/src/keyboard_customization.c"
			"marigold/src/project_diagnostics.c"
			"marigold/src/power_button_x86.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_AMD_STT
	"marigold/src/stt.c")

endif()
