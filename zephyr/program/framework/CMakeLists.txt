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
zephyr_library_sources("src/ucsi.c")
zephyr_library_sources("src/temperature_filter.c")
zephyr_library_sources("src/power_button_x86.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_DEDICATED_ALS
	"src/als.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_HID
	"src/hid_device.c")
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
zephyr_library_sources_ifdef(CONFIG_CHIPSET_AMD
	"src/cypd_amd.c")
zephyr_library_sources_ifdef(CONFIG_CHIPSET_INTEL
	"src/cypd_intel.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_TOUCHPAD_CUSTOMIZED
	"src/ps2mouse.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGE_MANAGER
	"src/battery_extender.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_CHASSIS_OPEN_SWITCH
	"src/chassis.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FRAMEWORK_MINI_PC
	"src/diagnostics_minipc.c")
zephyr_library_sources_ifndef(CONFIG_PLATFORM_EC_FRAMEWORK_MINI_PC
	"src/diagnostics_laptop.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_WAKE_ON_LAN
	"src/wol.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER_RAA489300
	"src/raa489300.c")

if (NOT DEFINED CONFIG_PLATFORM_EC_FRAMEWORK_MINI_PC)
	zephyr_library_sources("src/laptop_led.c")
endif ()

if(DEFINED CONFIG_BOARD_LOTUS OR DEFINED CONFIG_BOARD_TULIP)
	if(DEFINED CONFIG_BOARD_LOTUS)
		project(lotus)
		cros_ec_library_include_directories_ifdef(CONFIG_BOARD_LOTUS include)
		zephyr_library_sources("lotus/src/pmf.c")
	elseif(DEFINED CONFIG_BOARD_TULIP)
		project(tulip)
		cros_ec_library_include_directories_ifdef(CONFIG_BOARD_TULIP include)
		zephyr_library_sources("tulip/src/pmf.c")
	endif()
	zephyr_include_directories("include/lotus")
	zephyr_library_sources(
			"lotus/src/battery.c"
			"lotus/src/board_function.c"
			"lotus/src/cpu_power.c"
			"lotus/src/input_module.c"
			"lotus/src/gpu.c"
			"lotus/src/power_sequence.c"
			"lotus/src/f75303.c"
			"lotus/src/fan.c"
			"lotus/src/project_diagnostics.c"
			"lotus/src/amd_r23m.c"
			"lotus/src/ej889i.c"
			"lotus/src/thermal.c"
			"lotus/src/gpu_configuration.c"
			"lotus/src/gpu_f75303.c"
			"lotus/src/gpu_temp.c"
			"lotus/src/nv_gn22.c"
			"lotus/src/led.c"
			"src/usbc_5port_config.c"
			"src/ucsi_port_16.c"
			"src/temperature_filter.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_AMD_STT
	"lotus/src/stt.c")

	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER_ISL9241
	"lotus/src/charger.c")

	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER_BQ25770
	"tulip/src/charger.c")
endif()

if(DEFINED CONFIG_BOARD_AZALEA)
	project(azalea)
	cros_ec_library_include_directories_ifdef(CONFIG_BOARD_AZALEA include)
	zephyr_include_directories("include/framework13")
	zephyr_library_sources(
			"src/battery_12_13.c"
			"src/ucsi_port_13.c"
			"src/battery_present_adc.c"
			"azalea/src/charger.c"
			"azalea/src/cpu_power.c"
			"azalea/src/power_sequence.c"
			"azalea/src/project_diagnostics.c"
			"src/usbc_4port_config.c"
			"src/keyboard_customization_13.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_AMD_STT
	"azalea/src/stt.c")

endif()

if(DEFINED CONFIG_BOARD_MARIGOLD OR DEFINED CONFIG_BOARD_SAKURA)
	if(DEFINED CONFIG_BOARD_MARIGOLD)
		project(marigold)
		cros_ec_library_include_directories_ifdef(CONFIG_BOARD_MARIGOLD include)
		zephyr_library_sources("marigold/src/charger.c")
		zephyr_library_sources("marigold/src/power_sequence.c")
		zephyr_library_sources("marigold/src/usbc.c")
		zephyr_library_sources("src/ucsi_port_13.c")
		zephyr_library_sources("marigold/src/thermal.c")
		zephyr_library_sources("marigold/src/cpu_power.c")
	elseif(DEFINED CONFIG_BOARD_SAKURA)
		project(sakura)
		cros_ec_library_include_directories_ifdef(CONFIG_BOARD_SAKURA include)
		zephyr_library_sources("sakura/src/charger.c")
		zephyr_library_sources("sakura/src/power_sequence.c")
		zephyr_library_sources("sakura/src/usbc.c")
		zephyr_library_sources("sakura/src/ucsi_port.c")
		zephyr_library_sources("src/input_module_13.c")
		zephyr_library_sources("sakura/src/cpu_power.c")
		zephyr_library_sources("sakura/src/led.c")
		zephyr_library_sources("sakura/src/battery.c")
	endif()
	zephyr_include_directories("include/framework13")
	zephyr_library_sources(
			"src/battery_12_13.c"
			"src/battery_present_adc.c"
			"src/keyboard_customization_13.c"
			"marigold/src/project_diagnostics.c"
			"marigold/src/temperature.c"
			"src/usbc_4port_config.c"
	)

endif()

if(DEFINED CONFIG_BOARD_SUNFLOWER)
	project(sunflower)
	cros_ec_library_include_directories_ifdef(CONFIG_BOARD_SUNFLOWER include)
	zephyr_include_directories("include/sunflower")
	zephyr_library_sources(
			"src/battery_12_13.c"
			"src/ucsi_port_12.c"
			"sunflower/src/battery_present.c"
			"sunflower/src/charger.c"
			"sunflower/src/cpu_power.c"
			"sunflower/src/power_sequence.c"
			"sunflower/src/project_diagnostics.c"
			"sunflower/src/keyboard_customization.c"
			"sunflower/src/motionsense.c"
			"sunflower/src/temperature.c"
			"sunflower/src/touchpad.c"
			"sunflower/src/board_tablet_mode.c"
			"sunflower/src/usbc.c"
			"src/usbc_4port_config.c"
	)

endif()
