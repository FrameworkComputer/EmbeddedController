# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Note: this cmake system implements only a zephyr module, and is not
# intended to build a complete EC.  To build projects in platform/ec,
# you should continue to use the Makefile system.
#
# Googlers can find the design doc at go/zephyr-shim.

if(NOT DEFINED ZEPHYR_CURRENT_MODULE_DIR)
  message(FATAL_ERROR "This Cmake system implements only a Zephyr module, and
          should not be invoked directly.  Please continue to use the Makefile
          system for non-Zephyr builds.")
endif()

set(PLATFORM_EC "${ZEPHYR_CURRENT_MODULE_DIR}" CACHE PATH
    "Path to the platform/ec repo.")
# Zephyr 2.3 will set ZEPHYR_CURRENT_MODULE_DIR to the directory of the
# CMakeLists.txt file, whereas 2.4 will set it to the actual module
# directory.  Try to detect the condition by searching for
# zephyr/module.yml.
if(NOT EXISTS "${PLATFORM_EC}/zephyr/module.yml")
  set(PLATFORM_EC "${PLATFORM_EC}/..")
  assert_exists("${PLATFORM_EC}/zephyr/module.yml")
endif()

if(DEFINED ZMAKE_INCLUDE_DIR)
  zephyr_include_directories("${ZMAKE_INCLUDE_DIR}")
endif()

if(DEFINED CONFIG_PLATFORM_EC)
  # Add CHROMIUM_EC definition, which is used by ec_commands.h to
  # determine that the header is being compiled for the EC instead of
  # by another third-party C codebase.
  zephyr_compile_definitions("CHROMIUM_EC")

  # Add CONFIG_ZEPHYR, which is commonly used to guard code for use
  # with Zephyr builds only.
  zephyr_compile_definitions("CONFIG_ZEPHYR")

  # Force compiler warnings to generate errors
  option(ALLOW_WARNINGS "Do not treat warnings as errors")
  if (NOT ALLOW_WARNINGS)
    zephyr_compile_options(-Werror)
  endif()

  include(fpu.cmake)

  # When LTO is enabled, enable only for the "app" library, which compiles
  # and links all Chromium OS sources.
  # TODO: Enable LTO for all sources when Zephyr supports it.
  # See https://github.com/zephyrproject-rtos/zephyr/issues/2112
  if (DEFINED CONFIG_LTO)
    # The Zephyr toolchain generates linker errors if both CONFIG_LTO and
    # CONFIG_FPU are used. See b/184302085.
    if(("${ZEPHYR_TOOLCHAIN_VARIANT}" STREQUAL "zephyr") AND
     (DEFINED CONFIG_FPU))
      message(STATUS "Zephyr toolchain and CONFIG_FPU detected: disabling LTO")
    else()
      set_property(TARGET app PROPERTY INTERPROCEDURAL_OPTIMIZATION True)
    endif()
  endif()

  set_compiler_property(APPEND PROPERTY warning_base
                        # TODO(b/210501420): Make EC code compatible with
                        # -W-address-of-packed-member
                        -Wno-address-of-packed-member
  )
endif()

# Switch from the "zephyr" library to the "app" library for all Chromium OS
# sources.
set(ZEPHYR_CURRENT_LIBRARY app)

add_subdirectory(linker)

zephyr_library_include_directories(include)

if (DEFINED CONFIG_PLATFORM_EC)
  zephyr_library_include_directories(
    "${PLATFORM_EC}/zephyr/shim/include"
    "${PLATFORM_EC}/fuzz"
    "${PLATFORM_EC}/test"
    "${PLATFORM_EC}"
    "${PLATFORM_EC}/include"
    "${PLATFORM_EC}/include/driver"
    "${PLATFORM_EC}/third_party")
endif()

add_subdirectory("app")
add_subdirectory("drivers")
add_subdirectory("emul")

add_subdirectory_ifdef(CONFIG_PLATFORM_EC "shim")
# Creates a phony target all.libraries in case you only want to build the
# libraries and not the binaries. For example for creating the initial zero
# coverage files.
get_property(ZEPHYR_LIBS_PROPERTY GLOBAL PROPERTY ZEPHYR_LIBS)
add_custom_target(
  all.libraries
  DEPENDS
  ${ZEPHYR_LIBS_PROPERTY}
  kernel
  ${CMAKE_BINARY_DIR}/gcov.sh
  )
configure_file(gcov.tmpl.sh ${CMAKE_BINARY_DIR}/gcov.sh)

# CONFIG_PLATFORM_EC files that don't relate to something below should be
# included here, sorted by filename. This is common functionality which is
# supported by all boards and emulators (including unit tests) using the shim
# layer.
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC "${PLATFORM_EC}/common/base32.c"
                                                "${PLATFORM_EC}/common/console_output.c"
                                                "${PLATFORM_EC}/common/ec_features.c"
                                                "${PLATFORM_EC}/common/gpio_commands.c"
                                                "${PLATFORM_EC}/common/peripheral.c"
                                                "${PLATFORM_EC}/common/printf.c"
                                                "${PLATFORM_EC}/common/queue.c"
                                                "${PLATFORM_EC}/common/shared_mem.c"
                                                "${PLATFORM_EC}/common/uart_printf.c"
                                                "${PLATFORM_EC}/common/util.c"
                                                "${PLATFORM_EC}/common/version.c")

# Now include files that depend on or relate to other CONFIG options, sorted by
# CONFIG
zephyr_library_sources_ifdef(CONFIG_HAS_TASK_POWERBTN
                                                "${PLATFORM_EC}/common/power_button_x86.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCEL_BMA255
                                                "${PLATFORM_EC}/driver/accel_bma2x2.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCEL_KX022
                                                "${PLATFORM_EC}/driver/accel_kionix.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCEL_LIS2DW12
                                                "${PLATFORM_EC}/driver/accel_lis2dw12.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCELGYRO_BMI
                                                "${PLATFORM_EC}/driver/accelgyro_bmi_common.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCELGYRO_BMI160
                                                "${PLATFORM_EC}/driver/accelgyro_bmi160.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCELGYRO_BMI260
                                                "${PLATFORM_EC}/driver/accelgyro_bmi260.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCELGYRO_ICM
                                                "${PLATFORM_EC}/driver/accelgyro_icm_common.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCELGYRO_ICM426XX
                                                "${PLATFORM_EC}/driver/accelgyro_icm426xx.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCEL_FIFO
                                                "${PLATFORM_EC}/common/motion_sense_fifo.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ADC_CMD
                                                "${PLATFORM_EC}/common/adc.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ALS_TCS3400
                                                "${PLATFORM_EC}/driver/als_tcs3400.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACPI
                                                "${PLATFORM_EC}/common/acpi.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_BACKLIGHT_LID
                                                "${PLATFORM_EC}/common/backlight_lid.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_BATTERY
                                                "${PLATFORM_EC}/common/battery.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_BATTERY_V1
                                                "${PLATFORM_EC}/common/battery_v1.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_BATTERY_V2
                                                "${PLATFORM_EC}/common/battery_v2.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_BATTERY_FUEL_GAUGE
                                                "${PLATFORM_EC}/common/battery_fuel_gauge.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_BATTERY_SMART
                                                "${PLATFORM_EC}/driver/battery/smart.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_BC12_DETECT_PI3USB9201
                                                "${PLATFORM_EC}/driver/bc12/pi3usb9201.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_BC12_DETECT_MT6360
                                                "${PLATFORM_EC}/driver/bc12/mt6360.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER_ISL9237
                                                "${PLATFORM_EC}/driver/charger/isl923x.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER_ISL9238
                                                "${PLATFORM_EC}/driver/charger/isl923x.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER_ISL9238C
                                                "${PLATFORM_EC}/driver/charger/isl923x.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER_ISL9241
                                                "${PLATFORM_EC}/driver/charger/isl9241.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER_BQ25710
                                                "${PLATFORM_EC}/driver/charger/bq25710.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER_BQ25720
                                                "${PLATFORM_EC}/driver/charger/bq25710.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGE_MANAGER
                                                "${PLATFORM_EC}/common/charger.c"
                                                "${PLATFORM_EC}/common/charge_manager.c"
                                                "${PLATFORM_EC}/common/charge_state_v2.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGE_RAMP_HW
                                                "${PLATFORM_EC}/common/charge_ramp.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGE_RAMP_SW
                                                "${PLATFORM_EC}/common/charge_ramp.c"
                                                "${PLATFORM_EC}/common/charge_ramp_sw.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CBI_EEPROM
                                                "${PLATFORM_EC}/common/cbi.c"
                                                "${PLATFORM_EC}/common/cbi_eeprom.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CBI_GPIO
                                                "${PLATFORM_EC}/common/cbi.c"
                                                "${PLATFORM_EC}/common/cbi_gpio.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CONSOLE_CMD_MEM
                                                "${PLATFORM_EC}/common/memory_commands.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_DPTF
                                                "${PLATFORM_EC}/common/dptf.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ
                                                "${PLATFORM_EC}/common/chipset.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ESPI
                                                "${PLATFORM_EC}/common/espi.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC "${PLATFORM_EC}/common/extpower_common.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_EXTPOWER_GPIO
                                                "${PLATFORM_EC}/common/extpower_gpio.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN
                                                "${PLATFORM_EC}/common/fan.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FLASH_CROS
                                                "${PLATFORM_EC}/common/flash.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_HOSTCMD
                                                "${PLATFORM_EC}/common/host_command.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_HOSTCMD
                                                "${PLATFORM_EC}/common/host_event_commands.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE
                                                "${PLATFORM_EC}/common/uart_hostcmd.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_HOSTCMD_GET_UPTIME_INFO
                                                "${PLATFORM_EC}/common/uptime.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_HOSTCMD_REGULATOR
                                                "${PLATFORM_EC}/common/regulator.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_I2C
                                                "${PLATFORM_EC}/common/i2c_controller.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_I2C_DEBUG
                                                "${PLATFORM_EC}/common/i2c_trace.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_I2C_VIRTUAL_BATTERY
                                                "${PLATFORM_EC}/common/virtual_battery.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CONSOLE_CMD_IOEX
                                                "${PLATFORM_EC}/common/ioexpander_commands.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_IOEX_CCGXXF
                                                "${PLATFORM_EC}/driver/ioexpander/ccgxxf.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_IOEX_IT8801
                                                "${PLATFORM_EC}/driver/ioexpander/it8801.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_IOEX_NCT38XX
                                                "${PLATFORM_EC}/driver/ioexpander/ioexpander_nct38xx.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_IOEX_PCA9675
                                                "${PLATFORM_EC}/driver/ioexpander/pca9675.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_IOEX_PCAL6408
                                                "${PLATFORM_EC}/driver/ioexpander/pcal6408.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_IOEX_TCA64XXA
                                                "${PLATFORM_EC}/driver/ioexpander/tca64xxa.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD
                                                "${PLATFORM_EC}/common/keyboard_scan.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_PROTOCOL_8042
                                                "${PLATFORM_EC}/common/keyboard_8042.c"
                                                "${PLATFORM_EC}/common/keyboard_8042_sharedlib.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_PROTOCOL_MKBP
                                                "${PLATFORM_EC}/common/keyboard_mkbp.c"
                                                "${PLATFORM_EC}/common/mkbp_fifo.c"
                                                "${PLATFORM_EC}/common/mkbp_info.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_MKBP_INPUT_DEVICES
                                                "${PLATFORM_EC}/common/mkbp_input_devices.c"
                                                "${PLATFORM_EC}/common/mkbp_fifo.c"
                                                "${PLATFORM_EC}/common/mkbp_info.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_VIVALDI
                                                "${PLATFORM_EC}/common/keyboard_vivaldi.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_PWM_KBLIGHT
                                                "${PLATFORM_EC}/common/keyboard_backlight.c"
                                                "${PLATFORM_EC}/common/pwm_kblight.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_COMMON
                                                "${PLATFORM_EC}/common/led_common.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_PWM
                                                "${PLATFORM_EC}/common/led_pwm.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_ONOFF_STATES
                                                "${PLATFORM_EC}/common/led_onoff_states.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LID_ANGLE
                                                "${PLATFORM_EC}/common/motion_lid.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LID_ANGLE_UPDATE
                                                "${PLATFORM_EC}/common/lid_angle.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LID_SWITCH
                                                "${PLATFORM_EC}/common/lid_switch.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_MKBP_EVENT
                                                "${PLATFORM_EC}/common/mkbp_event.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_MOTIONSENSE
                                                "${PLATFORM_EC}/common/motion_sense.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_MP2964
                                                "${PLATFORM_EC}/driver/mp2964.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_PORT80
                                                "${PLATFORM_EC}/common/port80.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_POWER_BUTTON
                                                "${PLATFORM_EC}/common/power_button.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ
                                                "${PLATFORM_EC}/power/common.c")
zephyr_library_sources_ifdef(CONFIG_CHIPSET_ALDERLAKE_SLG4BD44540
                                                "${PLATFORM_EC}/power/alderlake_slg4bd44540.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ_AMD
                                                "${PLATFORM_EC}/power/amd_x86.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ_COMETLAKE
                                                "${PLATFORM_EC}/power/cometlake.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ_ICELAKE
                                                "${PLATFORM_EC}/power/icelake.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ_INTEL
                                                "${PLATFORM_EC}/power/intel_x86.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ_HOST_SLEEP
                                                "${PLATFORM_EC}/power/host_sleep.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ_MT8192
                                                "${PLATFORM_EC}/power/mt8192.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ_SC7180
                                                "${PLATFORM_EC}/power/qcom.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ_SC7280
                                                "${PLATFORM_EC}/power/qcom.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_PANIC
                                                "${PLATFORM_EC}/common/panic_output.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_PWM
                                                "${PLATFORM_EC}/common/pwm.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_SHA256_SW
                                                "${PLATFORM_EC}/common/sha256.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_SWITCH
                                                "${PLATFORM_EC}/common/switch.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_SWITCHCAP_LN9310
                                                "${PLATFORM_EC}/driver/ln9310.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_SPI_FLASH_REGS
                                                "${PLATFORM_EC}/common/spi_flash_reg.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_STM_MEMS_COMMON
                                                "${PLATFORM_EC}/driver/stm_mems_common.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_TABLET_MODE
                                                "${PLATFORM_EC}/common/tablet_mode.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_TEMP_SENSOR
                                                "${PLATFORM_EC}/common/thermal.c"
                                                "${PLATFORM_EC}/common/temp_sensor.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_THERMISTOR
                                                "${PLATFORM_EC}/driver/temp_sensor/thermistor.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_TEMP_SENSOR_TMP112
                                                "${PLATFORM_EC}/driver/temp_sensor/tmp112.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_THROTTLE_AP
                                                "${PLATFORM_EC}/common/throttle_ap.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_TIMER
                                                "${PLATFORM_EC}/common/timer.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_CHARGER
                                                "${PLATFORM_EC}/common/usb_charger.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PORT_POWER_DUMB
                                                "${PLATFORM_EC}/common/usb_port_power_dumb.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_POWER_DELIVERY
                                                "${PLATFORM_EC}/common/usb_common.c"
                                                "${PLATFORM_EC}/common/usb_pd_pdo.c"
                                                "${PLATFORM_EC}/common/usbc/usbc_task.c"
                                                "${PLATFORM_EC}/common/usbc/usb_pd_timer.c"
                                                "${PLATFORM_EC}/common/usbc/usb_sm.c"
                                                "${PLATFORM_EC}/common/usbc_intr_task.c"
                                                "${PLATFORM_EC}/common/usb_pd_flags.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CONSOLE_CMD_CHARGEN
                                                "${PLATFORM_EC}/common/chargen.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CONSOLE_CMD_PD
                                                "${PLATFORM_EC}/common/usbc/usb_pd_console.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_RETIMER_FW_UPDATE
                                                "${PLATFORM_EC}/common/usbc/usb_retimer_fw_update.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_RETIMER_INTEL_BB
                                                "${PLATFORM_EC}/driver/retimer/bb_retimer.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_RETIMER_PS8818
                                                "${PLATFORM_EC}/driver/retimer/ps8818.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_RETIMER_ANX7451
                                                "${PLATFORM_EC}/driver/usb_mux/anx7451.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_SS_MUX
                                                "${PLATFORM_EC}/driver/usb_mux/usb_mux.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_MUX_AMD_FP6
                                                "${PLATFORM_EC}/driver/usb_mux/amd_fp6.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_MUX_IT5205
                                                "${PLATFORM_EC}/driver/usb_mux/it5205.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_MUX_PS8743
                                                "${PLATFORM_EC}/driver/usb_mux/ps8743.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_MUX_VIRTUAL
                                                "${PLATFORM_EC}/driver/usb_mux/virtual.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_LOGGING
                                                "${PLATFORM_EC}/common/event_log.c"
                                                "${PLATFORM_EC}/common/pd_log.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TBT_COMPAT_MODE
                                                "${PLATFORM_EC}/common/usbc/tbt_alt_mode.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_USB4
                                                "${PLATFORM_EC}/common/usbc/usb_mode.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_OCP
                                                "${PLATFORM_EC}/common/usbc_ocp.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_ALT_MODE_DFP
                                                "${PLATFORM_EC}/common/usb_pd_alt_mode_dfp.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_ALT_MODE_UFP
                                                "${PLATFORM_EC}/common/usb_pd_alt_mode_ufp.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_DUAL_ROLE
                                                "${PLATFORM_EC}/common/usb_pd_dual_role.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_CONSOLE_CMD
                                                "${PLATFORM_EC}/common/usb_pd_console_cmd.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_HOST_CMD
                                                "${PLATFORM_EC}/common/usb_pd_host_cmd.c"
                                                "${PLATFORM_EC}/common/usbc/usb_pd_host.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_VPD
                                                "${PLATFORM_EC}/common/usbc/usb_tc_vpd_sm.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_CTVPD
                                                "${PLATFORM_EC}/common/usbc/usb_tc_ctvpd_sm.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_DRP_ACC_TRYSRC
                                                "${PLATFORM_EC}/common/usbc/usb_tc_drp_acc_trysrc_sm.c"
                                                "${PLATFORM_EC}/common/usbc/usb_pe_drp_sm.c"
                                                "${PLATFORM_EC}/common/usbc/usb_pd_dpm.c"
						"${PLATFORM_EC}/common/usbc/usbc_pd_policy.c"
                                                "${PLATFORM_EC}/common/usbc/dp_alt_mode.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PRL_SM
                                                "${PLATFORM_EC}/common/usbc/usb_prl_sm.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_ANX7447
                                                "${PLATFORM_EC}/driver/tcpm/anx7447.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_NCT38XX
                                                "${PLATFORM_EC}/driver/tcpm/nct38xx.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_PS8751
                                                "${PLATFORM_EC}/driver/tcpm/ps8xxx.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_PS8805
                                                "${PLATFORM_EC}/driver/tcpm/ps8xxx.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_PS8815
                                                "${PLATFORM_EC}/driver/tcpm/ps8xxx.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_RT1715
                                                "${PLATFORM_EC}/driver/tcpm/rt1715.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_RT1718S
                                                "${PLATFORM_EC}/driver/tcpm/rt1718s.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_TUSB422
                                                "${PLATFORM_EC}/driver/tcpm/tusb422.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_TCPCI
                                                "${PLATFORM_EC}/driver/tcpm/tcpci.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_ITE_ON_CHIP
                                                "${PLATFORM_EC}/driver/tcpm/ite_pd_intc.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_DRIVER_IT83XX
                                                "${PLATFORM_EC}/driver/tcpm/it83xx.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_DRIVER_IT8XXX2
                                                "${PLATFORM_EC}/driver/tcpm/it8xxx2.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_PPC
                                                "${PLATFORM_EC}/common/usbc_ppc.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_PPC_AOZ1380
                                                "${PLATFORM_EC}/driver/ppc/aoz1380.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_PPC_NX20P3483
                                                "${PLATFORM_EC}/driver/ppc/nx20p348x.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_PPC_SN5S330
                                                "${PLATFORM_EC}/driver/ppc/sn5s330.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_PPC_SYV682X
                                                "${PLATFORM_EC}/driver/ppc/syv682x.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_PPC_RT1718S
                                                "${PLATFORM_EC}/driver/ppc/rt1718s.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_VBOOT_HASH
                                                "${PLATFORM_EC}/common/vboot_hash.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_VOLUME_BUTTONS
                                                "${PLATFORM_EC}/common/button.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_VBOOT_EFS2
                                                "${PLATFORM_EC}/common/vboot/efs2.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_VSTORE
                                                "${PLATFORM_EC}/common/vstore.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_RTC
						"${PLATFORM_EC}/common/rtc.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_MATH_UTIL
						"${PLATFORM_EC}/common/math_util.c")
