# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources(app PRIVATE src/common.c)

# Alt charger
if (DEFINED CONFIG_TEST_BOARD_ALT_CHARGER)
	target_sources(app PRIVATE
		${PLATFORM_EC_PROGRAM_DIR}/skyrim/${CONFIG_TEST_BOARD_NAME}/src/alt_charger.c
		src/alt_charger.c
	)
endif()

# Fan
if (DEFINED CONFIG_TEST_BOARD_FAN)
	target_sources(app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/skyrim/${CONFIG_TEST_BOARD_NAME}/src/fan.c)
	if (NOT DEFINED CONFIG_TEST_BOARD_FAN_CUSTOM)
		target_sources(app PRIVATE src/fan.c)
	endif()
endif()

# PPC config
if (DEFINED CONFIG_TEST_BOARD_PPC_CONFIG)
	target_sources(app PRIVATE ${PLATFORM_EC}/zephyr/shim/src/ppc.c)
	target_sources(app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/skyrim/${CONFIG_TEST_BOARD_NAME}/src/ppc_config.c)
	target_sources(app PRIVATE src/ppc_config.c)
endif()

# USB mux config
if (DEFINED CONFIG_TEST_BOARD_USB_MUX_CONFIG)
	target_sources(app PRIVATE
		${PLATFORM_EC}/driver/usb_mux/usb_mux.c
		${PLATFORM_EC}/zephyr/shim/src/usb_muxes.c
		src/usb_mux_config.c
		${PLATFORM_EC_PROGRAM_DIR}/skyrim/src/usb_mux_config.c
	)
endif()

# USB-C fakes
if (CONFIG_PLATFORM_EC_USBC)
  target_sources(app PRIVATE src/usbc_fakes.c)
endif()
