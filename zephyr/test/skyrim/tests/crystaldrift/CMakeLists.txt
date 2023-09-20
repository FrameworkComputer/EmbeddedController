# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources(app PRIVATE src/common.c)
target_sources_ifdef(CONFIG_TEST_BOARD_PPC_CONFIG app PRIVATE src/ppc_config.c)
target_sources_ifdef(CONFIG_TEST_BOARD_FORM_FACTOR app PRIVATE
	../skyrim/src/form_factor.c ${PLATFORM_EC_PROGRAM_DIR}/skyrim/${CONFIG_TEST_BOARD_NAME}/src/form_factor.c)
target_sources_ifdef(CONFIG_TEST_BOARD_USB_MUX_CONFIG app PRIVATE
        ${PLATFORM_EC_PROGRAM_DIR}/skyrim/${CONFIG_TEST_BOARD_NAME}/src/usb_mux_config.c
        src/usb_mux_config.c
)
target_sources_ifdef(
        CONFIG_TEST_BOARD_FAN_CUSTOM
        app PRIVATE src/fan.c
        ${PLATFORM_EC_PROGRAM_DIR}/skyrim/crystaldrift/src/thermal.c)
