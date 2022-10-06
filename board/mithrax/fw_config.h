/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_MITHRAX_FW_CONFIG_H_
#define __BOARD_MITHRAX_FW_CONFIG_H_

#include <stdint.h>

/****************************************************************************
 * CBI FW_CONFIG layout for mithrax board.
 *
 * Source of truth is the project/brya/mithrax/config.star configuration file.
 */

enum ec_cfg_usb_db_type { DB_USB_ABSENT = 0, DB_USB3_PS8815 = 1 };

enum ec_cfg_keyboard_backlight_type {
	KEYBOARD_BACKLIGHT_DISABLED = 0,
	KEYBOARD_BACKLIGHT_ENABLED = 1
};

enum ec_cfg_usb_mb_type { NA = 0, MB_USB3_NON_TBT = 1 };

enum ec_cfg_stylus_type { STYLUS_ABSENT = 0, STYLUS_PRSENT = 1 };

enum ec_cfg_kb_backlight_type { SOLID_COLOR = 0, RGB = 1 };

union mithrax_cbi_fw_config {
	struct {
		enum ec_cfg_usb_db_type usb_db : 3;
		uint32_t wifi : 1;
		enum ec_cfg_kb_backlight_type rgb : 1;
		enum ec_cfg_stylus_type stylus : 1;
		enum ec_cfg_keyboard_backlight_type kb_bl : 1;
		uint32_t audio : 3;
		uint32_t thermal : 2;
		uint32_t table_mode : 1;
		enum ec_cfg_usb_mb_type usb_mb : 3;
		uint32_t reserved_1 : 16;
	};
	uint32_t raw_value;
};

/**
 * Read the cached FW_CONFIG.  Guaranteed to have valid values.
 *
 * @return the FW_CONFIG for the board.
 */
union mithrax_cbi_fw_config get_fw_config(void);

/**
 * Get the USB daughter board type from FW_CONFIG.
 *
 * @return the USB daughter board type.
 */
enum ec_cfg_usb_db_type ec_cfg_usb_db_type(void);

/**
 * Get the USB main board type from FW_CONFIG.
 *
 * @return the USB main board type.
 */
enum ec_cfg_usb_mb_type ec_cfg_usb_mb_type(void);

#endif /* __BOARD_MITHRAX_FW_CONFIG_H_ */

/**
 * Get the stylus type from FW_CONFIG.
 *
 * @return the stylus type.
 */
enum ec_cfg_stylus_type ec_cfg_stylus(void);

/**
 * Get the rgb type from FW_CONFIG.
 *
 * @return the rgb type.
 */
enum ec_cfg_kb_backlight_type ec_cfg_kb_backlight(void);
