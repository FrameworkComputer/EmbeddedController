# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(intelrvp)

cros_ec_library_include_directories(include)
cros_ec_library_include_directories("${PLATFORM_EC}/driver/charger")
cros_ec_library_include_directories("${PLATFORM_EC}/driver/ppc")
cros_ec_library_include_directories("${PLATFORM_EC}/driver/tcpm")
cros_ec_library_include_directories("${PLATFORM_EC}/driver/usb_mux")
zephyr_library_sources("src/intelrvp.c")
zephyr_library_sources("src/intel_rvp_board_id.c")

if((DEFINED CONFIG_BOARD_ADLRVP_MCHP) OR (DEFINED CONFIG_BOARD_ADLRVP_NPCX))
  add_subdirectory(adlrvp)
  zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "${PLATFORM_EC}/baseboard/intelrvp/usb_pd_policy_mecc_1_0.c")
  zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "${PLATFORM_EC}/baseboard/intelrvp/chg_usb_pd_mecc_1_0.c")
  zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "${PLATFORM_EC}/baseboard/intelrvp/chg_usb_pd.c")
  zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_PWM "src/intel_rvp_led.c")
endif()

if((DEFINED CONFIG_BOARD_MTLRVP_M1723) OR (DEFINED CONFIG_BOARD_MTLRVP_MCHP) OR
    (DEFINED CONFIG_BOARD_MTLRVP_NPCX) OR (DEFINED CONFIG_BOARD_MTLRVP_PD))
  add_subdirectory(mtlrvp)
  zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPMV2 "src/usb_pd_policy_mecc_1_1.c")
  zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPMV2 "src/chg_usb_pd_mecc_1_1.c")
  zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPMV2 "src/chg_usb_tcpc.c")
  zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_DEDICATED_CHARGE_PORT "src/dc_jack.c")
endif()

if(DEFINED CONFIG_BOARD_PTLRVP_COMMON)
  add_subdirectory(ptlrvp)
  zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_DEDICATED_CHARGE_PORT "src/dc_jack.c")
endif()
