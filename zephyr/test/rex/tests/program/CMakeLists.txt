# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_include_directories(src)
target_sources_ifdef(CONFIG_TEST_PROGRAM_USB_PD_POLICY app PRIVATE src/usb_pd_policy.c)
target_sources_ifdef(CONFIG_TEST_PROGRAM_USB_PD_POLICY app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/rex/src/usb_pd_policy.c)

target_sources_ifdef(CONFIG_TEST_PROGRAM_BOARD_POWER app PRIVATE src/board_power.c)
target_sources_ifdef(CONFIG_TEST_PROGRAM_BOARD_POWER app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/rex/src/board_power.c)

target_sources_ifdef(CONFIG_TEST_PROGRAM_BATTERY_PRESENT app PRIVATE src/battery_present.c)
target_sources_ifdef(CONFIG_TEST_PROGRAM_BATTERY_PRESENT app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/rex/src/battery_present.c)
target_sources_ifdef(CONFIG_TEST_PROGRAM_BATTERY_PRESENT app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/rex/src/usb_pd_policy.c)
