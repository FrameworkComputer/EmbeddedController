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

if(DEFINED CONFIG_PLATFORM_EC)
  # Build the EC's expected ec_version.h file.
  set(ec_version_file "${CMAKE_BINARY_DIR}/ec/include/generated/ec_version.h")
  # Create the command used to generate the ec_version.h file.
  add_custom_command(
          OUTPUT ${ec_version_file}
          COMMAND ${PLATFORM_EC}/util/getversion.sh BOARD=${BOARD} > ${ec_version_file}
          WORKING_DIRECTORY ${PLATFORM_EC}
  )
  # Create a custom target that will depend on the file.
  add_custom_target(generate_ec_version DEPENDS ${ec_version_file})

  # Create a local library (ec_version) that will be a pure interface library
  # and will depend on the custom target that generates the ec_version.h file.
  add_library(ec_version INTERFACE)
  add_dependencies(ec_version generate_ec_version)

  # Register the library with zephyr so that it generates the file at build
  # time. Also, append the include directory so we can include ec_version.h.
  zephyr_append_cmake_library(ec_version)
  zephyr_include_directories("${CMAKE_BINARY_DIR}/ec/include/generated")

  # Add CHROMIUM_EC definition, which is used by ec_commands.h to
  # determine that the header is being compiled for the EC instead of
  # by another third-party C codebase.
  zephyr_compile_definitions("CHROMIUM_EC")

  # Add CONFIG_ZEPHYR, which is commonly used to guard code for use
  # with Zephyr builds only.
  zephyr_compile_definitions("CONFIG_ZEPHYR")
endif()

# If a header is needed (should only be set for npcx) run the ecst.py script to
# generate it.
if (DEFINED CONFIG_PLATFORM_EC_RO_HEADER)
    # Translate the CONFIG_FLASH_SIZE to the correct argument value.
    if ("${CONFIG_FLASH_SIZE}" STREQUAL "512" OR "${CONFIG_FLASH_SIZE}" STREQUAL "1024")
        set(flash_size 1)
    elseif ("${CONFIG_FLASH_SIZE}" STREQUAL "2048")
        set(flash_size 2)
    elseif ("${CONFIG_FLASH_SIZE}" STREQUAL "4096")
        set(flash_size 4)
    elseif ("${CONFIG_FLASH_SIZE}" STREQUAL "8192")
        set(flash_size 8)
    elseif ("${CONFIG_FLASH_SIZE}" STREQUAL "16384")
        set(flash_size 16)
    endif()

    if (DEFINED CONFIG_PLATFORM_EC_RO_HEADER_CORE_CLOCK_SPI_CLOCK_RATIO_1)
        set(spiclkratio 1)
    elseif (DEFINED CONFIG_PLATFORM_EC_RO_HEADER_CORE_CLOCK_SPI_CLOCK_RATIO_2)
        set(spiclkratio 2)
    else()
        # Defaults to 1 if none are set.
        set(spiclkratio 1)
    endif()

    if (DEFINED CONFIG_PLATFORM_EC_RO_HEADER_SPI_MAX_CLOCK_20)
        set(spimaxclk 20)
    elseif (DEFINED CONFIG_PLATFORM_EC_RO_HEADER_SPI_MAX_CLOCK_25)
        set(spimaxclk 25)
    elseif (DEFINED CONFIG_PLATFORM_EC_RO_HEADER_SPI_MAX_CLOCK_33)
        set(spimaxclk 33)
    elseif (DEFINED CONFIG_PLATFORM_EC_RO_HEADER_SPI_MAX_CLOCK_40)
        set(spimaxclk 40)
    elseif (DEFINED CONFIG_PLATFORM_EC_RO_HEADER_SPI_MAX_CLOCK_50)
        set(spimaxclk 50)
    else()
        # Defaults to 20 if none are set.
        set(spimaxclk 20)
    endif()

    if (DEFINED CONFIG_PLATFORM_EC_RO_HEADER_SPI_READ_MODE_NORMAL)
        set(spireadmode "normal")
    elseif (DEFINED CONFIG_PLATFORM_EC_RO_HEADER_SPI_READ_MODE_FAST)
        set(spireadmode "fast")
    elseif (DEFINED CONFIG_PLATFORM_EC_RO_HEADER_SPI_READ_MODE_DUAL)
        set(spireadmode "dual")
    elseif (DEFINED CONFIG_PLATFORM_EC_RO_HEADER_SPI_READ_MODE_QUAD)
        set(spireadmode "quad")
    else()
        # Defaults to "dual" if none are set.
        set(spireadmode "dual")
    endif()

    # Check for disabling header CRC.
    if (NOT DEFINED CONFIG_PLATFORM_EC_RO_HEADER_ENABLE_HEADER_CRC)
        set(hcrc "-nohcrc")
    endif()

    # Check for disabling firmware CRC.
    if (NOT DEFINED CONFIG_PLATFORM_EC_RO_HEADER_ENABLE_FIRMWARE_CRC)
        set(fcrc "-nofcrc")
    endif()

    # Note, we cannot get the chip from Kconfig because currently Zephyr doesn't
    # support all the variations we need for NPCX. These values should be set
    # in each project's board.cmake.
    set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
            COMMAND ${PYTHON_EXECUTABLE}
            ${ZEPHYR_BASE}/boards/arm/npcx7m6fb_evb/support/ecst.py
            -i ${PROJECT_BINARY_DIR}/${CONFIG_KERNEL_BIN_NAME}.bin
            -o ${PROJECT_BINARY_DIR}/zephyr.packed.bin ${hcrc} ${fcrc}
            -chip ${ECST_CHIP_ARG} -flashsize ${flash_size}
            -spiclkratio ${spiclkratio} -spimaxclk ${spimaxclk}
            -spireadmode ${spireadmode})
endif()

add_subdirectory(linker)

zephyr_include_directories(include)

zephyr_include_directories_ifdef(
  CONFIG_PLATFORM_EC
  "${PLATFORM_EC}/zephyr/shim/include"
  "${PLATFORM_EC}/fuzz"
  "${PLATFORM_EC}/test"
  "${PLATFORM_EC}/include"
  "${PLATFORM_EC}/include/driver"
  "${PLATFORM_EC}/third_party")

add_subdirectory("app")
add_subdirectory("drivers")

add_subdirectory_ifdef(CONFIG_PLATFORM_EC "shim")

# CONFIG_PLATFORM_EC files that don't relate to something below should be
# included here, sorted by filename. This is common functionality which is
# supported by all boards and emulators (including unit tests) using the shim
# layer.
zephyr_sources_ifdef(CONFIG_PLATFORM_EC         "${PLATFORM_EC}/common/base32.c"
                                                "${PLATFORM_EC}/common/console_output.c"
                                                "${PLATFORM_EC}/common/ec_features.c"
                                                "${PLATFORM_EC}/common/gpio_commands.c"
                                                "${PLATFORM_EC}/common/peripheral.c"
                                                "${PLATFORM_EC}/common/printf.c"
                                                "${PLATFORM_EC}/common/queue.c"
                                                "${PLATFORM_EC}/common/shared_mem.c"
                                                "${PLATFORM_EC}/common/uart_printf.c"
                                                "${PLATFORM_EC}/common/version.c")

# Now include files that depend on or relate to other CONFIG options, sorted by
# CONFIG
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_ACCEL_BMA255
                                                "${PLATFORM_EC}/driver/accel_bma2x2.c"
                                                "${PLATFORM_EC}/common/math_util.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_ACCELGYRO_BMI260
                                                "${PLATFORM_EC}/driver/accelgyro_bmi_common.c"
                                                "${PLATFORM_EC}/driver/accelgyro_bmi260.c"
                                                "${PLATFORM_EC}/common/math_util.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_ACCEL_FIFO
                                                "${PLATFORM_EC}/common/motion_sense_fifo.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_ADC_CMD "${PLATFORM_EC}/common/adc.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_ALS_TCS3400
                                                "${PLATFORM_EC}/driver/als_tcs3400.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_ACPI    "${PLATFORM_EC}/common/acpi.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_BACKLIGHT_LID
                                                "${PLATFORM_EC}/common/backlight_lid.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_BATTERY "${PLATFORM_EC}/common/battery.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_BATTERY_FUEL_GAUGE
                                                "${PLATFORM_EC}/common/battery_fuel_gauge.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_BATTERY_SMART
                                                "${PLATFORM_EC}/driver/battery/smart.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_BC12_DETECT_PI3USB9201
                                                "${PLATFORM_EC}/driver/bc12/pi3usb9201.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER_ISL9241
                                                "${PLATFORM_EC}/driver/charger/isl9241.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_CHARGE_MANAGER
                                                "${PLATFORM_EC}/common/charger.c"
                                                "${PLATFORM_EC}/common/charge_manager.c"
                                                "${PLATFORM_EC}/common/charge_state_v2.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_CHARGE_RAMP_SW
                                                "${PLATFORM_EC}/common/charge_ramp.c"
                                                "${PLATFORM_EC}/common/charge_ramp_sw.c")

zephyr_sources_ifdef(CONFIG_PLATFORM_EC_CBI     "${PLATFORM_EC}/common/cbi.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_CONSOLE_CMD_MEM
                                                "${PLATFORM_EC}/common/memory_commands.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_DPTF    "${PLATFORM_EC}/common/dptf.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ
                                                "${PLATFORM_EC}/common/chipset.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_ESPI    "${PLATFORM_EC}/common/espi.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC         "${PLATFORM_EC}/common/extpower_common.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_EXTPOWER_GPIO
                                                "${PLATFORM_EC}/common/extpower_gpio.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_FAN     "${PLATFORM_EC}/common/fan.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_FLASH   "${PLATFORM_EC}/common/flash.c"
                                                "${PLATFORM_EC}/common/spi_flash_reg.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_HOSTCMD "${PLATFORM_EC}/common/host_command.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_HOSTCMD "${PLATFORM_EC}/common/host_event_commands.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE
                                                "${PLATFORM_EC}/common/uart_hostcmd.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_HOSTCMD_GET_UPTIME_INFO
                                                "${PLATFORM_EC}/common/uptime.c")

zephyr_sources_ifdef(CONFIG_PLATFORM_EC_I2C     "${PLATFORM_EC}/common/i2c_controller.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD
                                                "${PLATFORM_EC}/common/keyboard_scan.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_PROTOCOL_8042
                                                "${PLATFORM_EC}/common/keyboard_8042.c"
                                                "${PLATFORM_EC}/common/keyboard_8042_sharedlib.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_VIVALDI
                                                "${PLATFORM_EC}/common/keyboard_vivaldi.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_PWM_KBLIGHT
                                                "${PLATFORM_EC}/common/keyboard_backlight.c"
                                                "${PLATFORM_EC}/common/pwm_kblight.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_LED_COMMON
                                                "${PLATFORM_EC}/common/led_common.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_LED_PWM "${PLATFORM_EC}/common/led_pwm.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_LID_ANGLE
                                                "${PLATFORM_EC}/common/motion_lid.c"
                                                "${PLATFORM_EC}/common/math_util.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_LID_ANGLE_UPDATE
                                                "${PLATFORM_EC}/common/lid_angle.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_LID_SWITCH
                                                "${PLATFORM_EC}/common/lid_switch.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_MOTIONSENSE
                                                "${PLATFORM_EC}/common/motion_sense.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_MKBP_EVENT
                                                "${PLATFORM_EC}/common/mkbp_event.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_PORT80  "${PLATFORM_EC}/common/port80.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_POWER_BUTTON
                                                "${PLATFORM_EC}/common/power_button.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ
                                                "${PLATFORM_EC}/power/common.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ_COMETLAKE
                                                "${PLATFORM_EC}/power/cometlake.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ_ICELAKE
                                                "${PLATFORM_EC}/power/icelake.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ_INTEL
                                                "${PLATFORM_EC}/common/power_button_x86.c"
                                                "${PLATFORM_EC}/power/intel_x86.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ_HOST_SLEEP
                                                "${PLATFORM_EC}/power/host_sleep.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_PANIC   "${PLATFORM_EC}/common/panic_output.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_PWM     "${PLATFORM_EC}/common/pwm.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_SHA256_SW
                                                "${PLATFORM_EC}/common/sha256.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_SWITCH
                                                "${PLATFORM_EC}/common/switch.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_SWITCHCAP_LN9310
                                                "${PLATFORM_EC}/driver/ln9310.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_SPI_FLASH_REGS
                                                "${PLATFORM_EC}/common/spi_flash_reg.c")

zephyr_sources_ifdef(CONFIG_PLATFORM_EC_TABLET_MODE
                                                "${PLATFORM_EC}/common/tablet_mode.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_TEMP_SENSOR
                                                "${PLATFORM_EC}/common/thermal.c"
                                                "${PLATFORM_EC}/common/temp_sensor.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_THERMISTOR
                                                "${PLATFORM_EC}/driver/temp_sensor/thermistor.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_THROTTLE_AP
                                                "${PLATFORM_EC}/common/throttle_ap.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_TIMER   "${PLATFORM_EC}/common/timer.c")

zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_CHARGER
                                                "${PLATFORM_EC}/common/usb_charger.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_PORT_POWER_DUMB
                                                "${PLATFORM_EC}/common/usb_port_power_dumb.c")

zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_POWER_DELIVERY
                                                "${PLATFORM_EC}/common/usb_common.c"
                                                "${PLATFORM_EC}/common/usbc/usbc_task.c"
                                                "${PLATFORM_EC}/common/usbc/usb_pd_timer.c"
                                                "${PLATFORM_EC}/common/usbc/usb_sm.c"
                                                "${PLATFORM_EC}/common/usbc_intr_task.c")

zephyr_sources_ifdef(CONFIG_PLATFORM_EC_CONSOLE_CMD_CHARGEN
                                                "${PLATFORM_EC}/common/chargen.c")

zephyr_sources_ifdef(CONFIG_PLATFORM_EC_CONSOLE_CMD_PD
                                                "${PLATFORM_EC}/common/usbc/usb_pd_console.c")

zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USBC_RETIMER_FW_UPDATE
                                                "${PLATFORM_EC}/common/usbc/usb_retimer_fw_update.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USBC_RETIMER_INTEL_BB
                                                "${PLATFORM_EC}/driver/retimer/bb_retimer.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USBC_SS_MUX
                                                "${PLATFORM_EC}/driver/usb_mux/usb_mux.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_MUX_VIRTUAL
                                                "${PLATFORM_EC}/driver/usb_mux/virtual.c")

zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_LOGGING
                                                "${PLATFORM_EC}/common/event_log.c"
                                                "${PLATFORM_EC}/common/pd_log.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TBT_COMPAT_MODE
                                                "${PLATFORM_EC}/common/usbc/tbt_alt_mode.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_USB4
                                                "${PLATFORM_EC}/common/usbc/usb_mode.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USBC_OCP
                                                "${PLATFORM_EC}/common/usbc_ocp.c")

zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_ALT_MODE_DFP
                                                "${PLATFORM_EC}/common/usb_pd_alt_mode_dfp.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_ALT_MODE_UFP
                                                "${PLATFORM_EC}/common/usb_pd_alt_mode_ufp.c")

zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_DUAL_ROLE
                                                "${PLATFORM_EC}/common/usb_pd_dual_role.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_CONSOLE_CMD
                                                "${PLATFORM_EC}/common/usb_pd_console_cmd.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_HOST_CMD
                                                "${PLATFORM_EC}/common/usb_pd_host_cmd.c"
                                                "${PLATFORM_EC}/common/usbc/usb_pd_host.c")

zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_VPD "${PLATFORM_EC}/common/usbc/usb_tc_vpd_sm.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_CTVPD
                                                "${PLATFORM_EC}/common/usbc/usb_tc_ctvpd_sm.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_DRP_ACC_TRYSRC
                                                "${PLATFORM_EC}/common/usbc/usb_tc_drp_acc_trysrc_sm.c"
                                                "${PLATFORM_EC}/common/usbc/usb_pe_drp_sm.c"
                                                "${PLATFORM_EC}/common/usbc/usb_pd_dpm.c"
                                                "${PLATFORM_EC}/common/usbc/dp_alt_mode.c")

zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_PRL_SM
                                                "${PLATFORM_EC}/common/usbc/usb_prl_sm.c")

zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_PS8815
                                                "${PLATFORM_EC}/driver/tcpm/ps8xxx.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_RT1715
                                                "${PLATFORM_EC}/driver/tcpm/rt1715.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_TUSB422
                                                "${PLATFORM_EC}/driver/tcpm/tusb422.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_TCPCI
                                                "${PLATFORM_EC}/driver/tcpm/tcpci.c")

zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USBC_PPC
                                                "${PLATFORM_EC}/common/usbc_ppc.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USBC_PPC_SN5S330
                                                "${PLATFORM_EC}/driver/ppc/sn5s330.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USBC_PPC_SYV682X
                                                "${PLATFORM_EC}/driver/ppc/syv682x.c")

zephyr_sources_ifdef(CONFIG_PLATFORM_EC_VBOOT_HASH
                                                "${PLATFORM_EC}/common/vboot_hash.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_VOLUME_BUTTONS
                                                "${PLATFORM_EC}/common/button.c")

zephyr_sources_ifdef(CONFIG_PLATFORM_EC_VBOOT   "${PLATFORM_EC}/common/vboot/efs2.c")

zephyr_sources_ifdef(CONFIG_PLATFORM_EC_VSTORE  "${PLATFORM_EC}/common/vstore.c")

