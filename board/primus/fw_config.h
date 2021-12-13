/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_PRIMUS_FW_CONFIG_H_
#define __BOARD_PRIMUS_FW_CONFIG_H_

#include <stdint.h>

/****************************************************************************
 * CBI FW_CONFIG layout for Primus board.
 *
 * Source of truth is the project/brya/primus/config.star configuration file.
 */

enum ec_cfg_usb_db_type {
	DB_USB_ABSENT = 0,
	DB_USB3_PS8815 = 1,
	DB_USB_ABSENT2 = 15
};

enum ec_cfg_keyboard_backlight_type {
	KEYBOARD_BACKLIGHT_DISABLED = 0,
	KEYBOARD_BACKLIGHT_ENABLED = 1
};

enum ec_cfg_mlb_usb {
	MLB_USB_TBT = 0,
	MLB_USB_USB4 = 1
};

union primus_cbi_fw_config {
	struct {
		enum ec_cfg_usb_db_type			usb_db : 4;
		uint32_t				sd_db : 2;
		uint32_t				reserved_0 : 1;
		enum ec_cfg_keyboard_backlight_type	kb_bl : 1;
		uint32_t				audio : 3;
		uint32_t				cellular_db : 2;
		enum ec_cfg_mlb_usb			mlb_usb : 1;
		uint32_t				reserved_1 : 18;
	};
	uint32_t raw_value;
};

/**
 * Read the cached FW_CONFIG.  Guaranteed to have valid values.
 *
 * @return the FW_CONFIG for the board.
 */
union primus_cbi_fw_config get_fw_config(void);

/**
 * Get MLB USB type from FW_CONFIG.
 *
 * @return the MLB USB type.
 */
enum ec_cfg_mlb_usb ec_cfg_mlb_usb(void);

/**
 * Get the USB daughter board type from FW_CONFIG.
 *
 * @return the USB daughter board type.
 */
enum ec_cfg_usb_db_type ec_cfg_usb_db_type(void);

#endif /* __BOARD_PRIMUS_FW_CONFIG_H_ */
