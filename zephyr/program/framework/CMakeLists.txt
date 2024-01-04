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

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_COMMON
	"src/led_pwm.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_COMMON
	"src/led.c")
zephyr_library_sources_ifdef(CONFIG_CHIPSET_AMD
	"src/cpu_power/amd_cpu_power_interface.c")
zephyr_library_sources_ifdef(CONFIG_PECI_NPCX
	"src/cpu_power/intel_cpu_power_interface.c")

zephyr_library_sources_ifdef(CONFIG_PD_CHIP_CCG8
	"src/cypd_ccg8.c")
zephyr_library_sources_ifdef(CONFIG_PD_CHIP_CCG6
	"src/cypd_ccg6.c")

if(DEFINED CONFIG_BOARD_LOTUS)
	project(lotus)
	cros_ec_library_include_directories_ifdef(CONFIG_BOARD_LOTUS include)
	zephyr_include_directories("include/lotus")
	zephyr_library_sources(
			"lotus/src/battery.c"
			"lotus/src/charger.c"
			"lotus/src/cpu_power.c"
			"lotus/src/input_module.c"
			"lotus/src/gpu.c"
			"lotus/src/power_sequence.c"
			"lotus/src/f75303.c"
			"lotus/src/project_diagnostics.c"
			"lotus/src/power_button_x86.c"
			"lotus/src/amd_r23m.c"
			"lotus/src/ej889i.c"
			"lotus/src/thermal.c"
			"lotus/src/gpu_configuration.c"
			"lotus/src/gpu_f75303.c"


	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_AMD_STT
	"lotus/src/stt.c")

endif()

if(DEFINED CONFIG_BOARD_AZALEA)
	project(azalea)
	cros_ec_library_include_directories_ifdef(CONFIG_BOARD_AZALEA include)
	zephyr_include_directories("include/azalea")
	zephyr_library_sources(
			"azalea/src/battery.c"
			"azalea/src/charger.c"
			"azalea/src/cpu_power.c"
			"azalea/src/power_sequence.c"
			"azalea/src/keyboard_customization.c"
			"azalea/src/project_diagnostics.c"
			"azalea/src/power_button_x86.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_AMD_STT
	"azalea/src/stt.c")

endif()

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

endif()
