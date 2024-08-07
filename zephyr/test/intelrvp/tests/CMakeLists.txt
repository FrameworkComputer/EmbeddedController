# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

zephyr_include_directories("${PLATFORM_EC_PROGRAM_DIR}/intelrvp/include")
zephyr_include_directories(intelrvp/src/)

# Library sources from driver
zephyr_include_directories(${PLATFORM_EC}/driver/charger)
zephyr_include_directories(${PLATFORM_EC}/driver/ppc)
zephyr_include_directories(${PLATFORM_EC}/driver/usb_mux)
zephyr_include_directories(${PLATFORM_EC}/driver/tcpm)
zephyr_include_directories(${PLATFORM_EC}/driver/ioexpander)

if((CONFIG_TEST_PROJECT_MTLRVPP_NPCX) OR (CONFIG_TEST_PROJECT_MTLRVPP_MCHP) OR (CONFIG_TEST_PROJECT_MTLRVPP_COMMON))

#Add sources for MTLRVP boards.
target_sources(app PRIVATE "${PLATFORM_EC_PROGRAM_DIR}/intelrvp/src/intel_rvp_board_id.c")
target_sources(app PRIVATE "${PLATFORM_EC_PROGRAM_DIR}/intelrvp/src/dc_jack.c")
target_sources(app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/intelrvp/mtlrvp/src/board_power.c)
target_sources(app PRIVATE "${PLATFORM_EC_PROGRAM_DIR}/intelrvp/src/chg_usb_pd_mecc_1_1.c")
target_sources(app PRIVATE "${PLATFORM_EC_PROGRAM_DIR}/intelrvp/src/usb_pd_policy_mecc_1_1.c")
target_sources(app PRIVATE "${PLATFORM_EC_PROGRAM_DIR}/intelrvp/src/chg_usb_tcpc.c")

# Remove source files if tests provide fake functions
target_sources_ifndef(CONFIG_TEST_PROJECT_INTELRVP_TCPC app PRIVATE "${PLATFORM_EC_PROGRAM_DIR}/intelrvp/mtlrvp/src/mtlrvp.c")
target_sources_ifndef(CONFIG_TEST_PROJECT_INTELRVP_CHG_USB_PD_MECC app PRIVATE "${PLATFORM_EC_PROGRAM_DIR}/intelrvp/mtlrvp/src/tcpc.c")

# Common fake funtions
target_sources_ifndef(CONFIG_TEST_PROJECT_INTELRVP_TCPC app PRIVATE intelrvp/src/fakes.c)
endif()

target_sources_ifdef(CONFIG_TEST_PROJECT_MTL_BOARD_POWER app PRIVATE power/board_power.c)
target_sources_ifdef(CONFIG_TEST_PROJECT_INTELRVP_TCPC app PRIVATE mtlrvp/src/tcpc.c)
target_sources_ifdef(CONFIG_TEST_PROJECT_INTELRVP_CHG_USB_PD_MECC app PRIVATE intelrvp/src/chg_usb_pd_mecc_1_1.c)
target_sources_ifdef(CONFIG_TEST_PROJECT_INTELRVP_DC_JACK app PRIVATE intelrvp/src/dc_jack.c)
