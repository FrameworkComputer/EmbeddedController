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

zephyr_include_directories_ifdef(
  CONFIG_PLATFORM_EC
  "${PLATFORM_EC}/zephyr/shim/include"
  "${PLATFORM_EC}/fuzz"
  "${PLATFORM_EC}/test"
  "${PLATFORM_EC}/include"
  "${PLATFORM_EC}/include/driver")

add_subdirectory_ifdef(CONFIG_PLATFORM_EC "shim")

# CONFIG_PLATFORM_EC files that don't relate to something below should be
# included here, sorted by filename. This is common functionality which is
# supported by all boards and emulators (including unit tests) using the shim
# layer.
zephyr_sources_ifdef(CONFIG_PLATFORM_EC         "${PLATFORM_EC}/common/base32.c"
                                                "${PLATFORM_EC}/common/printf.c"
                                                "${PLATFORM_EC}/common/queue.c"
                                                "${PLATFORM_EC}/common/shared_mem.c")

# Now include files that depend on or relate to other CONFIG options, sorted by
# CONFIG
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_ACPI    "${PLATFORM_EC}/common/acpi.c"
                                                "${PLATFORM_EC}/common/ec_features.c")
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
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ
                                                "${PLATFORM_EC}/common/chipset.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_ESPI    "${PLATFORM_EC}/common/espi.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC         "${PLATFORM_EC}/common/extpower_common.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_EXTPOWER_GPIO
                                                "${PLATFORM_EC}/common/extpower_gpio.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_FLASH   "${PLATFORM_EC}/common/flash.c"
                                                "${PLATFORM_EC}/common/spi_flash_reg.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_HOSTCMD "${PLATFORM_EC}/common/host_command.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_HOSTCMD "${PLATFORM_EC}/common/host_event_commands.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_I2C     "${PLATFORM_EC}/common/i2c_controller.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD
                                                "${PLATFORM_EC}/common/keyboard_scan.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_PROTOCOL_8042
                                                "${PLATFORM_EC}/common/keyboard_8042.c"
                                                "${PLATFORM_EC}/common/keyboard_8042_sharedlib.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_LID_SWITCH
                                                "${PLATFORM_EC}/common/lid_switch.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_PORT80  "${PLATFORM_EC}/common/port80.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_POWER_BUTTON
                                                "${PLATFORM_EC}/common/power_button.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ
                                                "${PLATFORM_EC}/power/common.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ_ICELAKE
                                                "${PLATFORM_EC}/power/icelake.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ_INTEL
                                                "${PLATFORM_EC}/common/power_button_x86.c"
                                                "${PLATFORM_EC}/power/intel_x86.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_TIMER   "${PLATFORM_EC}/common/timer.c")

zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_CHARGER
                                                "${PLATFORM_EC}/common/usb_charger.c")

zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_POWER_DELIVERY
                                                "${PLATFORM_EC}/common/usb_common.c"
                                                "${PLATFORM_EC}/common/usbc/usb_sm.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USBC_OCP
                                                "${PLATFORM_EC}/common/usbc_ocp.c")

zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_ALT_MODE_DFP
                                                "${PLATFORM_EC}/common/usb_pd_alt_mode_dfp.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_DUAL_ROLE
                                                "${PLATFORM_EC}/common/usb_pd_dual_role.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_HOST_CMD
                                                "${PLATFORM_EC}/common/usb_pd_host_cmd.c")

zephyr_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPMV2
                                                "${PLATFORM_EC}/common/usbc/usb_sm.c"
                                                "${PLATFORM_EC}/common/usbc/usbc_task.c")

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

zephyr_sources_ifdef(CONFIG_SHELL               "${PLATFORM_EC}/common/gpio_commands.c")
