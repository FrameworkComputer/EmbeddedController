# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(drivers)

# Include the local test directory for shimmed_test_tasks.h
zephyr_include_directories("${CMAKE_CURRENT_SOURCE_DIR}")
zephyr_include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")
zephyr_include_directories("${PLATFORM_EC}/driver/ppc/")

target_sources(app PRIVATE
  src/battery.c
  src/bb_retimer.c
  src/bc12.c
  src/bma2x2.c
  src/bmi160.c
  src/bmi260.c
  src/charge_manager.c
  src/console_cmd/charge_manager.c
  src/console_cmd/charge_state.c
  src/cros_cbi.c
  src/espi.c
  src/gpio.c
  src/host_cmd/motion_sense.c
  src/integration/usbc/usb.c
  src/integration/usbc/usb_20v_3a_pd_charger.c
  src/integration/usbc/usb_5v_3a_pd_sink.c
  src/integration/usbc/usb_5v_3a_pd_source.c
  src/integration/usbc/usb_alt_mode.c
  src/integration/usbc/usb_attach_src_snk.c
  src/integration/usbc/usb_pd_ctrl_msg.c
  src/i2c_passthru.c
  src/isl923x.c
  src/keyboard_scan.c
  src/lid_switch.c
  src/lis2dw12.c
  src/ln9310.c
  src/main.c
  src/motion_sense/motion_sense.c
  src/panic.c
  src/power_common.c
  src/ppc_sn5s330.c
  src/ppc_syv682c.c
  src/ps8xxx.c
  src/smart.c
  src/stm_mems_common.c
  src/stubs.c
  src/tcpci.c
  src/tcpci_test_common.c
  src/tcs3400.c
  src/temp_sensor.c
  src/test_mocks.c
  src/test_rules.c
  src/thermistor.c
  src/usb_mux.c
  src/usb_pd_host_cmd.c
  src/utils.c
  src/vboot_hash.c
  src/watchdog.c
)

add_subdirectory(isl923x)
