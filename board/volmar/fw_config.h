/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_VOLMAR_FW_CONFIG_H_
#define __BOARD_VOLMAR_FW_CONFIG_H_

#include <stdint.h>

/****************************************************************************
 * CBI FW_CONFIG layout for Volmar board.
 *
 * Source of truth is the project/brya/volmar/config.star configuration file.
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

union volmar_cbi_fw_config {
	struct {
		enum ec_cfg_usb_db_type usb_db : 4;
		enum ec_cfg_keyboard_backlight_type kb_bl : 1;
		uint32_t audio : 3;
		uint32_t boot_nvme_mask : 1;
		uint32_t boot_emmc_mask : 1;
		uint32_t reserved_1 : 22;
	};
	uint32_t raw_value;
};

/**
 * Read the cached FW_CONFIG.  Guaranteed to have valid values.
 *
 * @return the FW_CONFIG for the board.
 */
union volmar_cbi_fw_config get_fw_config(void);

/**
 * Get the USB daughter board type from FW_CONFIG.
 *
 * @return the USB daughter board type.
 */
enum ec_cfg_usb_db_type ec_cfg_usb_db_type(void);

#endif /* __BOARD_VOLMAR_FW_CONFIG_H_ */
