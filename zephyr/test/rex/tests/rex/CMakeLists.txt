# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_include_directories("${PLATFORM_EC_PROGRAM_DIR}/rex/rex/include")

target_sources_ifdef(CONFIG_TEST_PROJECT_USB_MUX_CONFIG app PRIVATE
		${PLATFORM_EC_PROGRAM_DIR}/rex/rex/src/usb_mux_config.c
		${PLATFORM_EC_PROGRAM_DIR}/rex/src/usbc_config.c
		src/usb_mux_config.c
		src/fakes.c)

target_sources_ifdef(CONFIG_TEST_PROGRAM_I2C_POLICY app PRIVATE
		${PLATFORM_EC_PROGRAM_DIR}/rex/rex/src/i2c_policy.c
		${PLATFORM_EC_PROGRAM_DIR}/rex/src/usbc_config.c
		src/i2c_policy.c
		src/fakes.c)
