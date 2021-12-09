/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_TANIKS_FW_CONFIG_H_
#define __BOARD_TANIKS_FW_CONFIG_H_

#include <stdint.h>

/****************************************************************************
 * CBI FW_CONFIG layout for Taniks board.
 *
 * Source of truth is the project/taniks/taniks/config.star configuration file.
 */

enum ec_cfg_usb_db_type {
	DB_USB_ABSENT = 0,
	DB_USB3_PS8815 = 1
};

enum ec_cfg_keyboard_backlight_type {
	KEYBOARD_BACKLIGHT_DISABLED = 0,
	KEYBOARD_BACKLIGHT_ENABLED = 1
};

enum ec_cfg_tabletmode_type {
	TABLETMODE_DISABLED = 0,
	TABLETMODE_ENABLED = 1
};

union taniks_cbi_fw_config {
	struct {
		enum ec_cfg_usb_db_type			usb_db : 2;
		uint32_t				sd_db : 2;
		enum ec_cfg_keyboard_backlight_type	kb_bl : 1;
		uint32_t				audio : 3;
		uint32_t				reserved_1 : 6;
		/* b/211079131 - Fw config structure
		 * b/211076082 - Move tablet mode to bit14
		 * bit8-9: kb_layout
		 * bit10-11: wifi_sar_id,
		 * bit12: nvme
		 * bit13: emmc
		 */
		enum ec_cfg_tabletmode_type		tabletmode : 1;
		uint32_t				reserved_2 : 17;
	};
	uint32_t raw_value;
};

/**
 * Read the cached FW_CONFIG.  Guaranteed to have valid values.
 *
 * @return the FW_CONFIG for the board.
 */
union taniks_cbi_fw_config get_fw_config(void);

/**
 * Get the USB daughter board type from FW_CONFIG.
 *
 * @return the USB daughter board type.
 */
enum ec_cfg_usb_db_type ec_cfg_usb_db_type(void);

/**
 * Check if the FW_CONFIG has enabled keyboard backlight.
 *
 * @return true if board supports keyboard backlight, false if the board
 * doesn't support it.
 */
bool ec_cfg_has_keyboard_backlight(void);

/**
 * Check if the FW_CONFIG has enabled tablet mode.
 *
 * @return true if board supports tablet mode, false if the board
 * doesn't support it.
 */
bool ec_cfg_has_tabletmode(void);

#endif /* __BOARD_TANIKS_FW_CONFIG_H_ */
