# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

separate_arguments(EXTRA_EC_VERSION_FLAGS_LIST UNIX_COMMAND
		   ${EXTRA_EC_VERSION_FLAGS})

if(DEFINED CONFIG_PLATFORM_EC AND EXISTS "${EDT_PICKLE}")
  set(ENV{PYTHONPATH} "${PLATFORM_EC}/zephyr/:${PLATFORM_EC}/zephyr/zmake")

  message(STATUS "Performing EC specific devicetree checks")

  # Zephyr's gen_defines.py creates an EDT object of the final devicetree,
  # saving it as pickle file in the build directory.
  set(NAMED_GPIOS_SCRIPT ${PLATFORM_EC}/zephyr/scripts/named_gpios.py)
  set(CMD_NAMED_GPIOS ${PYTHON_EXECUTABLE} ${NAMED_GPIOS_SCRIPT}
    --zephyr-base ${ZEPHYR_BASE}
    --edt-pickle ${EDT_PICKLE}
  )
  execute_process(COMMAND ${CMD_NAMED_GPIOS} RESULT_VARIABLE ret)
  if(NOT "${ret}" STREQUAL "0")
    message(FATAL_ERROR "named_gpios.py failed with return code: ${ret}")
  endif()

  set(LED_POLICY_SCRIPT ${PLATFORM_EC}/zephyr/scripts/led_policy.py)
  set(CMD_LED_POLICY ${PYTHON_EXECUTABLE} ${LED_POLICY_SCRIPT}
    --zephyr-base ${ZEPHYR_BASE}
    --edt-pickle ${EDT_PICKLE}
  )
  execute_process(COMMAND ${CMD_LED_POLICY} RESULT_VARIABLE ret)
  if(NOT "${ret}" STREQUAL "0")
    message(FATAL_ERROR "led_policy.py failed with return code: ${ret}")
  endif()

  message(STATUS "Generating component manifest file")
  set(CME_SCRIPT ${PLATFORM_EC}/zephyr/scripts/cme.py)
  set(CMD_CME ${PYTHON_EXECUTABLE} ${CME_SCRIPT}
    --zephyr-base ${ZEPHYR_BASE}
    --edt-pickle ${EDT_PICKLE}
    --manifest-file "${CMAKE_BINARY_DIR}/zephyr/component_manifest.json"
    ${EXTRA_EC_VERSION_FLAGS_LIST}
  )
  execute_process(COMMAND ${CMD_CME} RESULT_VARIABLE ret)
  if(NOT "${ret}" STREQUAL "0")
    message(FATAL_ERROR "cme.py failed with return code: ${ret}")
  endif()
endif()

if(DEFINED ZMAKE_INCLUDE_DIR)
  zephyr_include_directories("${ZMAKE_INCLUDE_DIR}")
endif()

# When CONFIG_ASSERT is enabled, the __FILE__ macro may add full paths into the
# read-only strings. This wastes space and can cause non-reproducible builds.
# When the compiler supports it, replace common path prefixes with static
# strings.

# PLATFORM_EC points to the build directory, which symlinks to the actual
# source. ZEPHYR_CURRENT_CMAKE_DIR points to the actual source directory
# containing this file.  Set the PLATFORM_EC_SRC to the parent.
# Replace the paths of the both the build and source directories with "EC_BASE".
cmake_path(GET ZEPHYR_CURRENT_CMAKE_DIR PARENT_PATH PLATFORM_EC_SRC)
zephyr_cc_option(-fmacro-prefix-map=${PLATFORM_EC}=EC_BASE)
zephyr_cc_option(-fmacro-prefix-map=${PLATFORM_EC_SRC}=EC_BASE)

if(DEFINED CONFIG_PLATFORM_EC)
  # Add CHROMIUM_EC definition, which is used by ec_commands.h to
  # determine that the header is being compiled for the EC instead of
  # by another third-party C codebase.
  zephyr_compile_definitions("CHROMIUM_EC")

  # Add CONFIG_ZEPHYR, which is commonly used to guard code for use
  # with Zephyr builds only.
  zephyr_compile_definitions("CONFIG_ZEPHYR")

  include(fpu.cmake)

  # When LTO is enabled, enable only for the "app" library, which compiles
  # and links all Chromium OS sources.
  # TODO: Enable LTO for all sources when Zephyr supports it.
  # See https://github.com/zephyrproject-rtos/zephyr/issues/2112
  if (DEFINED CONFIG_PLATFORM_EC_LTO)
    # The Zephyr toolchain generates linker errors if both CONFIG_LTO and
    # CONFIG_FPU are used. See b/184302085.
    if(("${ZEPHYR_TOOLCHAIN_VARIANT}" STREQUAL "zephyr") AND
     (DEFINED CONFIG_FPU))
      message(STATUS "Zephyr toolchain and CONFIG_FPU detected: disabling LTO")
    else()
      set_property(TARGET app PROPERTY INTERPROCEDURAL_OPTIMIZATION True)
    endif()
  endif()
endif()

# Set extra compiler flags.
if (DEFINED CONFIG_ARM)
  # This flag is only used by ARM
  zephyr_cc_option(-mno-unaligned-access)
endif()
zephyr_cc_option(-Wimplicit-fallthrough)
zephyr_cc_option(-Wint-conversion)

if (DEFINED CONFIG_RISCV)
  zephyr_cc_option(-fsanitize=integer-divide-by-zero)
  zephyr_cc_option(-fsanitize-undefined-trap-on-error)
endif()

# TODO: b/323234432 - Investigate explicitly using doubles over floats
# For now disable the "double-promotion" warning to avoid errors from
# the sensor code and math_util.h.
get_target_property(warning_base_override compiler warning_base)

set(double_promotion -Wdouble-promotion)

if(${double_promotion} IN_LIST warning_base_override)
  list(REMOVE_ITEM warning_base_override ${double_promotion})
  set_compiler_property(PROPERTY warning_base ${warning_base_override})
  print(warning_base_override)
endif()


# The EC application is split into two libraries:
#   app: contains all the legacy code
#   ec_shim: contains all the code under platform/ec/shim
#
# This allows us to add an extra compiler definition __REQUIRE_ZEPHYR_GPIOS__
# that is applied only to the Zephyr sources. This is used to verify that
# all Zephyr sources are using the upstream Zephyr GPIO API and not the legacy
# EC API.
zephyr_library_named(ec_shim)
get_property(APP_LTO_PROPERTY TARGET app PROPERTY INTERPROCEDURAL_OPTIMIZATION)
set_property(TARGET ec_shim PROPERTY INTERPROCEDURAL_OPTIMIZATION ${APP_LTO_PROPERTY})
set_property(TARGET ec_shim APPEND PROPERTY COMPILE_DEFINITIONS __REQUIRE_ZEPHYR_GPIOS__)

# Create an interface library for CrosEC (cros_ec_interface) that can be used
# as a dependency for individual CrosEC libraries. To use:
#  zephyr_library_named(my_lib)
#  target_link_libraries(my_lib PRIVATE cros_ec_interface)
add_library(cros_ec_interface INTERFACE)

# Switch from the "zephyr" library to the "app" library for all Chromium OS
# sources.
set(ZEPHYR_CURRENT_LIBRARY app)

# Custom function that ensures the include path is always updated for both
# libraries.
function(cros_ec_library_include_directories)
  target_include_directories(app PUBLIC ${ARGN})
  target_include_directories(ec_shim PRIVATE ${ARGN})
  target_include_directories(cros_ec_interface INTERFACE ${ARGN})
endfunction()
function(cros_ec_library_include_directories_ifdef feature_toggle)
  if(${${feature_toggle}})
    target_include_directories(app PUBLIC ${ARGN})
    target_include_directories(ec_shim PRIVATE ${ARGN})
    target_include_directories(cros_ec_interface INTERFACE ${ARGN})
  endif()
endfunction()

cros_ec_library_include_directories(include)

if (DEFINED CONFIG_PLATFORM_EC)
  cros_ec_library_include_directories(
    "${PLATFORM_EC}/zephyr/shim/include"
    "${PLATFORM_EC}/fuzz"
    "${PLATFORM_EC}/test"
    "${PLATFORM_EC}"
    "${PLATFORM_EC}/include"
    "${PLATFORM_EC}/include/driver"
    "${PLATFORM_EC}/third_party")
endif()

if (ZEPHYR_PIGWEED_MODULE_DIR)
  include("${ZEPHYR_PIGWEED_MODULE_DIR}/pw_build/pigweed.cmake")
  if (DEFINED CONFIG_PIGWEED_LOG_TOKENIZED_LIB)
    add_library(log_tokenized_config INTERFACE)
    target_compile_options(log_tokenized_config INTERFACE -include
                           ${PLATFORM_EC}/zephyr/include/pigweed/pw_zephyr_tokenized_custom_log.h)
    pw_set_module_config(pw_log_tokenized_CONFIG log_tokenized_config)
    zephyr_include_directories("${PLATFORM_EC}/zephyr/include/pigweed")
  endif()
endif()

add_subdirectory("subsys")

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
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC
                                                # TODO(b/237712836): Remove once
                                                # Zephyr's libc has strcasecmp.
                                                "${PLATFORM_EC}/builtin/stdlib.c"
                                                "${PLATFORM_EC}/common/base32.c"
                                                "${PLATFORM_EC}/common/console_output.c"
                                                "${PLATFORM_EC}/common/ec_features.c"
                                                "${PLATFORM_EC}/common/gpio_commands.c"
                                                "${PLATFORM_EC}/common/peripheral.c"
                                                "${PLATFORM_EC}/common/printf.c"
                                                "${PLATFORM_EC}/common/queue.c"
                                                "${PLATFORM_EC}/common/shared_mem.c"
                                                "${PLATFORM_EC}/common/system.c"
                                                "${PLATFORM_EC}/common/system_boot_time.c"
                                                "${PLATFORM_EC}/common/uart_printf.c"
                                                "${PLATFORM_EC}/common/util.c"
                                                "${PLATFORM_EC}/common/version.c")

# Now include files that depend on or relate to other CONFIG options, sorted by
# CONFIG
zephyr_library_sources_ifdef(CONFIG_HAS_TASK_POWERBTN
                                                "${PLATFORM_EC}/common/power_button_x86.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCEL_BMA255
                                                "${PLATFORM_EC}/driver/accel_bma2x2.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCEL_BMA4XX
                                                "${PLATFORM_EC}/driver/accel_bma4xx.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCEL_KX022
                                                "${PLATFORM_EC}/driver/accel_kionix.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCEL_LIS2DE
                                                "${PLATFORM_EC}/driver/accel_lis2dh.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCEL_LIS2DS
                                                "${PLATFORM_EC}/driver/accel_lis2ds.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCEL_LIS2DW12
                                                "${PLATFORM_EC}/driver/accel_lis2dw12.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCELGYRO_BMI
                                                "${PLATFORM_EC}/driver/accelgyro_bmi_common.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCELGYRO_BMI160
                                                "${PLATFORM_EC}/driver/accelgyro_bmi160.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCELGYRO_BMI260
                                                "${PLATFORM_EC}/driver/accelgyro_bmi260.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCELGYRO_BMI3XX
                                                "${PLATFORM_EC}/driver/accelgyro_bmi3xx.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCELGYRO_ICM
                                                "${PLATFORM_EC}/driver/accelgyro_icm_common.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCELGYRO_ICM426XX
                                                "${PLATFORM_EC}/driver/accelgyro_icm426xx.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCELGYRO_ICM42607
                                                "${PLATFORM_EC}/driver/accelgyro_icm42607.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCELGYRO_LSM6DSO
                                                "${PLATFORM_EC}/driver/accelgyro_lsm6dso.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCELGYRO_LSM6DSM
                                                "${PLATFORM_EC}/driver/accelgyro_lsm6dsm.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCEL_FIFO
                                                "${PLATFORM_EC}/common/motion_sense_fifo.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_AMD_STB_DUMP
                                                "${PLATFORM_EC}/driver/amd_stb.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_BODY_DETECTION
                                                "${PLATFORM_EC}/common/body_detection.c")
zephyr_library_sources_ifdef(CONFIG_NAMED_ADC_CHANNELS
                                                "${PLATFORM_EC}/common/adc.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ALS_TCS3400
                                                "${PLATFORM_EC}/driver/als_tcs3400.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ALS_VEML3328
                                                "${PLATFORM_EC}/driver/als_veml3328.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ALS_CM32183
                                                "${PLATFORM_EC}/driver/als_cm32183.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACPI
                                                "${PLATFORM_EC}/common/acpi.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_AMD_SB_RMI
						"${PLATFORM_EC}/driver/sb_rmi.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_AMD_STT
						"${PLATFORM_EC}/driver/amd_stt.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_BACKLIGHT_LID
                                                "${PLATFORM_EC}/common/backlight_lid.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_DETACHABLE_BASE
                                                "${PLATFORM_EC}/common/base_state.c")
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
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_BC12_DETECT_RT1718S
                                                "${PLATFORM_EC}/driver/bc12/rt1718s.c")
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
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER_RAA489000
                                                "${PLATFORM_EC}/driver/charger/isl923x.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER_ISL9241
                                                "${PLATFORM_EC}/driver/charger/isl9241.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER_BQ25710
                                                "${PLATFORM_EC}/driver/charger/bq25710.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER_BQ25720
                                                "${PLATFORM_EC}/driver/charger/bq25710.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER_RT9490
                                                "${PLATFORM_EC}/driver/charger/rt9490.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER_SM5803
                                                "${PLATFORM_EC}/driver/charger/sm5803.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGESPLASH
                                                "${PLATFORM_EC}/common/chargesplash.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGE_MANAGER
                                                "${PLATFORM_EC}/common/charger.c"
                                                "${PLATFORM_EC}/common/charge_manager.c"
                                                "${PLATFORM_EC}/common/charge_state.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGE_RAMP_HW
                                                "${PLATFORM_EC}/common/charge_ramp.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGE_RAMP_SW
                                                "${PLATFORM_EC}/common/charge_ramp.c"
                                                "${PLATFORM_EC}/common/charge_ramp_sw.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CPS8100
                                                "${PLATFORM_EC}/driver/wpc/cps8100.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CURVE25519
                                                "${PLATFORM_EC}/common/curve25519.c")
if (NOT DEFINED CONFIG_CPU_CORTEX_M0)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CURVE25519
                                                "${PLATFORM_EC}/common/curve25519-generic.c")
endif()
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_OCPC
                                                "${PLATFORM_EC}/common/ocpc.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CBI
                                                "${PLATFORM_EC}/common/cbi.c"
                                                "${PLATFORM_EC}/common/cbi_config.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CBI_GPIO
                                                "${PLATFORM_EC}/common/cbi_gpio.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CEC
                                                "${PLATFORM_EC}/common/cec.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CEC_BITBANG
                                                "${PLATFORM_EC}/driver/cec/bitbang.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CEC_IT83XX
                                                "${PLATFORM_EC}/driver/cec/it83xx.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CONSOLE_CMD_MEM
                                                "${PLATFORM_EC}/common/memory_commands.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_DPTF
                                                "${PLATFORM_EC}/common/dptf.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ
                                                "${PLATFORM_EC}/common/chipset.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_AP_RESET_LOG
                                                "${PLATFORM_EC}/common/ap_reset_log.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_HOST_INTERFACE_ESPI
                                                "${PLATFORM_EC}/common/espi.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_EXTPOWER
						"${PLATFORM_EC}/common/extpower_common.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_EXTPOWER_GPIO
                                                "${PLATFORM_EC}/common/extpower_gpio.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN
                                                "${PLATFORM_EC}/common/fan.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FLASH_CROS
                                                "${PLATFORM_EC}/common/flash.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FINGERPRINT
						"${PLATFORM_EC}/common/fpsensor/fpsensor.cc"
						"${PLATFORM_EC}/common/fpsensor/fpsensor_state.cc"
						"${PLATFORM_EC}/common/fpsensor/fpsensor_crypto.cc"
						"${PLATFORM_EC}/common/fpsensor/fpsensor_debug.cc"
						"${PLATFORM_EC}/common/fpsensor/fpsensor_auth_commands.cc"
						"${PLATFORM_EC}/common/fpsensor/fpsensor_auth_crypto_stateful.cc"
						"${PLATFORM_EC}/common/fpsensor/fpsensor_auth_crypto_stateless.cc"
						"${PLATFORM_EC}/common/fpsensor/fpsensor_detect_strings.cc"
						"${PLATFORM_EC}/common/fpsensor/fpsensor_utils.cc"
						"${PLATFORM_EC}/crypto/elliptic_curve_key.cc")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_HOSTCMD
                                                "${PLATFORM_EC}/common/host_command.c")
if (NOT DEFINED CONFIG_EC_HOST_CMD)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_HOSTCMD
                                                "${PLATFORM_EC}/common/host_command_task.c")
endif()
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_HOSTCMD
                                                "${PLATFORM_EC}/common/host_event_commands.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE
                                                "${PLATFORM_EC}/common/uart_hostcmd.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_HOSTCMD_GET_UPTIME_INFO
                                                "${PLATFORM_EC}/common/uptime.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_HOSTCMD_REGULATOR
                                                "${PLATFORM_EC}/common/regulator.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_I2C
                                                "${PLATFORM_EC}/common/i2c_controller.c"
                                                "${PLATFORM_EC}/common/i2c_passthru.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_I2C_DEBUG
                                                "${PLATFORM_EC}/common/i2c_trace.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_I2C_VIRTUAL_BATTERY
                                                "${PLATFORM_EC}/common/virtual_battery.c")
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
zephyr_library_sources_ifdef(CONFIG_HAS_TASK_KEYSCAN
                                                "${PLATFORM_EC}/common/keyboard_scan.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_RGB_KEYBOARD
                                                "${PLATFORM_EC}/common/rgb_keyboard.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_PROTOCOL_8042
                                                "${PLATFORM_EC}/common/keyboard_8042.c"
                                                "${PLATFORM_EC}/common/keyboard_8042_sharedlib.c")
zephyr_library_sources_ifdef(CONFIG_MKBP_PROTOCOL
                                                "${PLATFORM_EC}/common/mkbp_fifo.c"
                                                "${PLATFORM_EC}/common/mkbp_info.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_PROTOCOL_MKBP
                                                "${PLATFORM_EC}/common/keyboard_mkbp.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_MKBP_INPUT_DEVICES
                                                "${PLATFORM_EC}/common/mkbp_input_devices.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_PWM_KBLIGHT
                                                "${PLATFORM_EC}/common/keyboard_backlight.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_COMMON
                                                "${PLATFORM_EC}/common/led_common.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_DT
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
                                                "${PLATFORM_EC}/common/mkbp_event.c"
                                                "${PLATFORM_EC}/common/mkbp_fifo.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_MOTIONSENSE
                                                "${PLATFORM_EC}/common/motion_sense.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_MP2964
                                                "${PLATFORM_EC}/driver/mp2964.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_PERIPHERAL_CHARGER
                                                "${PLATFORM_EC}/common/peripheral_charger.c")
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
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ_MT8186
                                                "${PLATFORM_EC}/power/mt8186.c")
# Re-use mt8186.c code
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ_MT8188
                                                "${PLATFORM_EC}/power/mt8186.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ_MT8192
                                                "${PLATFORM_EC}/power/mt8192.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ_SC7180
                                                "${PLATFORM_EC}/power/qcom.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ_SC7280
                                                "${PLATFORM_EC}/power/qcom.c")
if (CONFIG_PLATFORM_EC_HIBERNATE AND CONFIG_AP_PWRSEQ AND NOT DEFINED CONFIG_EMUL_AP_PWRSEQ_DRIVER)
	zephyr_library_sources(			"${PLATFORM_EC}/power/hibernate.c")
endif ()
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_PANIC
                                                "${PLATFORM_EC}/common/panic_output.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_PSE_LTC4291
                                                "${PLATFORM_EC}/driver/pse_ltc4291.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_RSA
                                                "${PLATFORM_EC}/common/rsa.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_RWSIG
                                                "${PLATFORM_EC}/common/vboot/vb21_lib.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_RWSIG_VERIFY
                                                "${PLATFORM_EC}/common/rwsig.c"
                                                "${PLATFORM_EC}/common/vboot/common.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ROLLBACK
                                                "${PLATFORM_EC}/common/rollback.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_SHA256_SW
                                                "${PLATFORM_EC}/common/sha256.c")
if (NOT DEFINED CONFIG_PLATFORM_EC_SHA256_HW_ZEPHYR)
	zephyr_library_sources_ifdef(CONFIG_SOC_IT8XXX2_SHA256_HW_ACCELERATE
                                                "${PLATFORM_EC}/driver/sha256/sha256_it8xxx2.c")
endif()
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_SWITCH
                                                "${PLATFORM_EC}/common/switch.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_SWITCHCAP_LN9310
                                                "${PLATFORM_EC}/driver/ln9310.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_STM_MEMS_COMMON
                                                "${PLATFORM_EC}/driver/stm_mems_common.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_TABLET_MODE
                                                "${PLATFORM_EC}/common/tablet_mode.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_TEMP_SENSOR
                                                "${PLATFORM_EC}/common/thermal.c"
                                                "${PLATFORM_EC}/common/temp_sensor.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_THERMISTOR
                                                "${PLATFORM_EC}/driver/temp_sensor/thermistor.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_TEMP_SENSOR_PCT2075
                                                "${PLATFORM_EC}/driver/temp_sensor/pct2075.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_TEMP_SENSOR_SB_TSI
                                                "${PLATFORM_EC}/driver/temp_sensor/sb_tsi.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_TEMP_SENSOR_TMP112
                                                "${PLATFORM_EC}/driver/temp_sensor/tmp112.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_TEMP_SENSOR_F75303
                                                "${PLATFORM_EC}/driver/temp_sensor/f75303.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_THROTTLE_AP
                                                "${PLATFORM_EC}/common/throttle_ap.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_TIMER
                                                "${PLATFORM_EC}/common/timer.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_CHARGER
                                                "${PLATFORM_EC}/common/usb_charger.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PORT_POWER_DUMB
                                                "${PLATFORM_EC}/common/usb_port_power_dumb.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PORT_POWER_SMART
                                                "${PLATFORM_EC}/common/usb_port_power_smart.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_UPDATE
                                                "${PLATFORM_EC}/common/update_fw.c"
                                                "${PLATFORM_EC}/common/usb_update.c"
                                                "${PLATFORM_EC}/common/queue_policies.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_POWER_DELIVERY
                                                "${PLATFORM_EC}/common/typec_control.c"
                                                "${PLATFORM_EC}/common/usb_common.c"
                                                "${PLATFORM_EC}/common/usb_pd_pdo.c"
                                                "${PLATFORM_EC}/common/usbc/usbc_task.c"
                                                "${PLATFORM_EC}/common/usbc/usb_pd_timer.c"
                                                "${PLATFORM_EC}/common/usbc/usb_sm.c"
                                                "${PLATFORM_EC}/common/usbc_intr_task.c"
                                                "${PLATFORM_EC}/common/usbc_utils.c"
                                                "${PLATFORM_EC}/common/usb_pd_flags.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_CONTROLLER
                                                "${PLATFORM_EC}/common/usbc_utils.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_TOUCHPAD_ELAN
                                                "${PLATFORM_EC}/driver/touchpad_elan.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CONSOLE_CMD_CHARGEN
                                                "${PLATFORM_EC}/common/chargen.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CONSOLE_CMD_PD
                                                "${PLATFORM_EC}/common/usbc/usb_pd_console.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_RETIMER_ANX7483
                                                "${PLATFORM_EC}/driver/retimer/anx7483.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_RETIMER_ANX7452
                                                "${PLATFORM_EC}/driver/retimer/anx7452.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_RETIMER_INTEL_BB
                                                "${PLATFORM_EC}/driver/retimer/bb_retimer.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_RETIMER_INTEL_HB
                                                "${PLATFORM_EC}/driver/retimer/bb_retimer.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_RETIMER_KB8010
                                                "${PLATFORM_EC}/driver/retimer/kb8010.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_RETIMER_PS8802
                                                "${PLATFORM_EC}/driver/retimer/ps8802.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_RETIMER_PS8818
                                                "${PLATFORM_EC}/driver/retimer/ps8818.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_RETIMER_PS8828
                                                "${PLATFORM_EC}/driver/retimer/ps8828.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_RETIMER_PS8833
                                                "${PLATFORM_EC}/driver/retimer/ps8833.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_RETIMER_PS8811
                                                "${PLATFORM_EC}/driver/retimer/ps8811.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_RETIMER_ANX7451
                                                "${PLATFORM_EC}/driver/usb_mux/anx7451.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_SS_MUX
                                                "${PLATFORM_EC}/driver/usb_mux/usb_mux.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_MUX_AMD_FP6
                                                "${PLATFORM_EC}/driver/usb_mux/amd_fp6.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_MUX_AMD_FP8
                                                "${PLATFORM_EC}/driver/usb_mux/amd_fp8.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_MUX_IT5205
                                                "${PLATFORM_EC}/driver/usb_mux/it5205.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_MUX_PS8743
                                                "${PLATFORM_EC}/driver/usb_mux/ps8743.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_MUX_TUSB546
                                                "${PLATFORM_EC}/driver/usb_mux/tusb1064.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_MUX_TUSB1044
                                                "${PLATFORM_EC}/driver/usb_mux/tusb1064.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_MUX_TUSB1064
                                                "${PLATFORM_EC}/driver/usb_mux/tusb1064.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_MUX_VIRTUAL
                                                "${PLATFORM_EC}/driver/usb_mux/virtual.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_DPS
                                                "${PLATFORM_EC}/common/dps.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_LOGGING
                                                "${PLATFORM_EC}/common/event_log.c"
                                                "${PLATFORM_EC}/common/pd_log.c")
if (CONFIG_PLATFORM_EC_USB_PD_TCPMV2)
	include(tcpmv2.cmake)
endif ()

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_HOST_CMD
                                                "${PLATFORM_EC}/common/usb_pd_host_cmd_common.c")

zephyr_library_sources_ifdef(CONFIG_SVDM_RSP_DFP_ONLY
                                                "${PLATFORM_EC}/common/usbc/svdm_rsp_dfp_only.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_OCP
                                                "${PLATFORM_EC}/common/usbc_ocp.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_DISCOVERY
                                                "${PLATFORM_EC}/common/usb_pd_discovery.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_ALT_MODE_UFP
                                                "${PLATFORM_EC}/common/usb_pd_alt_mode_ufp.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_DUAL_ROLE
                                                "${PLATFORM_EC}/common/usb_pd_dual_role.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_DP_HPD_GPIO
                                                "${PLATFORM_EC}/common/usbc/dp_hpd_gpio.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_VDM_AP_CONTROL
                                                "${PLATFORM_EC}/common/usbc/ap_vdm_control.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PRL_SM
                                                "${PLATFORM_EC}/common/usbc/usb_prl_sm.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_ANX7447
                                                "${PLATFORM_EC}/driver/tcpm/anx7447.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_CCGXXF
                                                "${PLATFORM_EC}/driver/tcpm/ccgxxf.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_NCT38XX
                                                "${PLATFORM_EC}/driver/tcpm/nct38xx.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_PS8XXX
                                                "${PLATFORM_EC}/driver/tcpm/ps8xxx.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_RAA489000
                                                "${PLATFORM_EC}/driver/tcpm/raa489000.c")
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
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_FUSB302
                                                "${PLATFORM_EC}/driver/tcpm/fusb302.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_PPC
                                                "${PLATFORM_EC}/common/usbc_ppc.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_PPC_AOZ1380
                                                "${PLATFORM_EC}/driver/ppc/aoz1380.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_PPC_KTU1125
                                                "${PLATFORM_EC}/driver/ppc/ktu1125.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_PPC_NX20P3481
                                                "${PLATFORM_EC}/driver/ppc/nx20p348x.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_PPC_NX20P3483
                                                "${PLATFORM_EC}/driver/ppc/nx20p348x.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_PPC_RT1739
                                                "${PLATFORM_EC}/driver/ppc/rt1739.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_PPC_SN5S330
                                                "${PLATFORM_EC}/driver/ppc/sn5s330.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_PPC_SYV682X
                                                "${PLATFORM_EC}/driver/ppc/syv682x.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_PPC_RT1718S
                                                "${PLATFORM_EC}/driver/ppc/rt1718s.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_VBOOT_HASH
                                                "${PLATFORM_EC}/common/vboot_hash.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_BUTTON
                                                "${PLATFORM_EC}/common/button.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_VBOOT_EFS2
                                                "${PLATFORM_EC}/common/vboot/efs2.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_VSTORE
                                                "${PLATFORM_EC}/common/vstore.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_RTC
						"${PLATFORM_EC}/common/rtc.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_MATH_UTIL
						"${PLATFORM_EC}/common/math_util.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_MAX695X_SEVEN_SEGMENT_DISPLAY
						"${PLATFORM_EC}/driver/led/max695x.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_DRIVER_TLC59116F
                                                "${PLATFORM_EC}/driver/led/tlc59116f.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_SYSTEM_SAFE_MODE
                                                "${PLATFORM_EC}/common/system_safe_mode.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_HOST_COMMAND_MEMORY_DUMP
                                                "${PLATFORM_EC}/common/host_command_memory_dump.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_AP_HANG_DETECT
                                                "${PLATFORM_EC}/common/ap_hang_detect.c")

# Switch to ec_shim library for all Zephyr sources
set(ZEPHYR_CURRENT_LIBRARY ec_shim)

add_subdirectory(linker)
add_subdirectory("app")
add_subdirectory("drivers")
add_subdirectory("emul")
add_subdirectory("fake")
add_subdirectory("mock")
add_subdirectory_ifdef(CONFIG_PLATFORM_EC "shim")
add_subdirectory_ifdef(CONFIG_HW_TEST_ON "test/hwtest")

# Use external script to generate the ec_version.h header and add as
# a dependency to the EC application library.
add_custom_target(
  ec_version_header
  VERBATIM COMMAND
  "PYTHONPATH=${PLATFORM_EC}/zephyr/zmake/" ${PYTHON_EXECUTABLE}
  "${PLATFORM_EC}/zephyr/zmake/zephyr_build_tools/generate_ec_version.py"
  "${CMAKE_CURRENT_BINARY_DIR}/include/ec_version.h"
  "--name" "${CMAKE_PROJECT_NAME}"
  ${EXTRA_EC_VERSION_FLAGS_LIST}
)
add_dependencies(app ec_version_header)

# Include the directory containing the generated header.
zephyr_include_directories("${CMAKE_CURRENT_BINARY_DIR}/include")

if (DEFINED CONFIG_PIGWEED_LOG_TOKENIZED_LIB)
  set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
    COMMAND
      vpython3 -vpython-spec ${PLATFORM_EC}/zephyr/pigweed-vpython3
      ${ZEPHYR_PIGWEED_MODULE_DIR}/pw_tokenizer/py/pw_tokenizer/database.py
      create
      --force
      --type binary
      -d ${CMAKE_BINARY_DIR}/database.bin
      ${CMAKE_BINARY_DIR}/zephyr/zephyr_pre0.elf
  )
endif()

if (NOT DEFINED CONFIG_BOARD_NATIVE_POSIX)
  set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
    COMMAND
      ${PYTHON_EXECUTABLE}
      ${PLATFORM_EC}/util/check_zephyr_end_of_ram.py
      ${CMAKE_BINARY_DIR}/zephyr/zephyr.elf
      --delete
  )
endif()
