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
  "${PLATFORM_EC}/include")

add_subdirectory_ifdef(CONFIG_PLATFORM_EC "shim")

zephyr_sources_ifdef(CONFIG_PLATFORM_EC         "${PLATFORM_EC}/common/base32.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ
                                                "${PLATFORM_EC}/common/chipset.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_ESPI    "${PLATFORM_EC}/common/espi.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC         "${PLATFORM_EC}/common/extpower_common.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_EXTPOWER_GPIO
                                                "${PLATFORM_EC}/common/extpower_gpio.c")
zephyr_sources_ifdef(CONFIG_SHELL               "${PLATFORM_EC}/common/gpio_commands.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_HOSTCMD "${PLATFORM_EC}/common/host_command.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_HOSTCMD "${PLATFORM_EC}/common/host_event_commands.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_I2C     "${PLATFORM_EC}/common/i2c_controller.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_PROTOCOL_8042
                                                "${PLATFORM_EC}/common/keyboard_8042.c"
                                                "${PLATFORM_EC}/common/keyboard_8042_sharedlib.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD
                                                "${PLATFORM_EC}/common/keyboard_scan.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_LID_SWITCH
                                                "${PLATFORM_EC}/common/lid_switch.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_PORT80  "${PLATFORM_EC}/common/port80.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_POWER_BUTTON
                                                "${PLATFORM_EC}/common/power_button.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ_INTEL
                                                "${PLATFORM_EC}/common/power_button_x86.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC         "${PLATFORM_EC}/common/queue.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_TIMER   "${PLATFORM_EC}/common/timer.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ
                                                "${PLATFORM_EC}/power/common.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ_INTEL
                                                "${PLATFORM_EC}/power/intel_x86.c")
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ_ICELAKE
                                                "${PLATFORM_EC}/power/icelake.c")
