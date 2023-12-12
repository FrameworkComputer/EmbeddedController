# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources(app PRIVATE src/common.c)
target_sources_ifdef(CONFIG_TEST_BOARD_I2C_POLICY app PRIVATE src/i2c_policy.c
			${PLATFORM_EC_PROGRAM_DIR}/skyrim/src/i2c_policy.c)
target_sources_ifdef(CONFIG_TEST_BOARD_USB_PD_POLICY app PRIVATE src/usb_pd_policy.c ${PLATFORM_EC_PROGRAM_DIR}/skyrim/src/usb_pd_policy.c)
target_sources_ifdef(CONFIG_TEST_BOARD_USBC_CONFIG app PRIVATE
    src/usbc_config.c
    ${PLATFORM_EC_PROGRAM_DIR}/skyrim/src/usbc_config.c
    ${PLATFORM_EC}/zephyr/shim/src/ppc.c
    ${PLATFORM_EC}/zephyr/shim/src/tcpc.c
)
target_sources_ifdef(CONFIG_TEST_BOARD_POWER_SIGNALS app PRIVATE src/power_signals ${PLATFORM_EC_PROGRAM_DIR}/skyrim/src/power_signals.c)
