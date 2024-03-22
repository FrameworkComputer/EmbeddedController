# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

set(PLATFORM_EC_BOARD "${PLATFORM_EC}/board/brya" CACHE PATH
    "Path to the platform/ec board directory")
set(PLATFORM_EC_BASEBOARD "${PLATFORM_EC}/baseboard/brya" CACHE PATH
    "Path to the platform/ec baseboard directory")

# Include board specific header files
zephyr_include_directories(
  include
  "${PLATFORM_EC}/driver/tcpm"
  "${PLATFORM_EC_BASEBOARD}"
  "${PLATFORM_EC_BOARD}")

if(DEFINED CONFIG_BOARD_BRYA)
  project(brya)
elseif(DEFINED CONFIG_BOARD_BRYA_PDC)
  project(brox_pdc)
  add_subdirectory(brya_pdc)
endif()

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_BATTERY
  "${PLATFORM_EC_BASEBOARD}/battery_presence.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CBI_EEPROM
  "${PLATFORM_EC_BASEBOARD}/cbi.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_PWM_KBLIGHT
  "kblight_hooks.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_BATTERY
  "${PLATFORM_EC_BASEBOARD}/battery_presence.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_BATTERY
  "battery_present.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_POWER_DELIVERY
  "${PLATFORM_EC_BASEBOARD}/usb_pd_policy.c")

if(NOT DEFINED CONFIG_BOARD_BRYA_PDC)
  zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC
    "${PLATFORM_EC_BOARD}/usbc_config.c"
    "${PLATFORM_EC_BOARD}/fw_config.c")
  zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGE_MANAGER
    "${PLATFORM_EC_BOARD}/charger.c"
    "${PLATFORM_EC}/common/math_util.c")
endif()
