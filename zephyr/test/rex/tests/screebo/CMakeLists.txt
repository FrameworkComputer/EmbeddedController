# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_include_directories("${PLATFORM_EC_PROGRAM_DIR}/rex/screebo/include")

target_sources_ifdef(CONFIG_TEST_PROJECT_USB_CONFIG app PRIVATE src/usb_config.c)
target_sources_ifdef(CONFIG_TEST_PROJECT_USB_CONFIG app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/rex/screebo/src/usb_config.c)
target_sources_ifdef(CONFIG_TEST_PROJECT_BOARD app PRIVATE src/board.c)
target_sources_ifdef(CONFIG_TEST_PROJECT_BOARD app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/rex/screebo/src/board.c)

target_sources_ifdef(CONFIG_TEST_PROGRAM_I2C_POLICY app PRIVATE src/i2c_policy.c)
target_sources_ifdef(CONFIG_TEST_PROGRAM_I2C_POLICY app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/rex/screebo/src/i2c_policy.c)
