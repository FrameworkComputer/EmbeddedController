/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __VOLTEER_CBI_EC_FW_CONFIG_H_
#define __VOLTEER_CBI_EC_FW_CONFIG_H_

#include "stdbool.h"
#include "stdint.h"

/****************************************************************************
 * CBI FW_CONFIG layout shared by all Volteer boards
 *
 * Source of truth is the program/volteer/program.star configuration file.
 */

enum ec_cfg_usb_db_type {
	DB_USB_ABSENT = 0,
	DB_USB4_GEN2 = 1,
	DB_USB3_ACTIVE = 2,
	DB_USB4_GEN3 = 3,
	DB_USB3_PASSIVE = 4,
	DB_USB3_NO_A = 5,
	DB_USB_COUNT
};

/*
 * Tablet Mode (1 bit), shared by all Volteer boards
 */
enum ec_cfg_tabletmode_type {
	TABLETMODE_DISABLED = 0,
	TABLETMODE_ENABLED = 1,
};

enum ec_cfg_keyboard_backlight_type {
	KEYBOARD_BACKLIGHT_DISABLED = 0,
	KEYBOARD_BACKLIGHT_ENABLED = 1
};

enum ec_cfg_numeric_pad_type {
	NUMERIC_PAD_DISABLED = 0,
	NUMERIC_PAD_ENABLED = 1
};

union volteer_cbi_fw_config {
	struct {
		enum ec_cfg_usb_db_type			usb_db : 4;
		uint32_t				thermal : 4;
		uint32_t				audio : 3;
		enum ec_cfg_tabletmode_type		tabletmode : 1;
		uint32_t				lte_db : 2;
		enum ec_cfg_keyboard_backlight_type	kb_bl : 1;
		enum ec_cfg_numeric_pad_type		num_pad : 1;
		uint32_t				sd_db : 4;
		uint32_t				reserved_2 : 12;
	};
	uint32_t raw_value;
};

/*
 * Each Volteer board must define the default FW_CONFIG options to use
 * if the CBI data has not been initialized.
 */
extern union volteer_cbi_fw_config fw_config_defaults;

/**
 * Initialize the FW_CONFIG from CBI data. If the CBI data is not valid, set the
 * FW_CONFIG to the board specific defaults.
 */
void init_fw_config(void);

/**
 * Read the cached FW_CONFIG.  Guaranteed to have valid values.
 *
 * @return the FW_CONFIG for the board.
 */
union volteer_cbi_fw_config get_fw_config(void);

/**
 * Get the USB daughter board type from FW_CONFIG.
 *
 * @return the USB daughter board type.
 */
enum ec_cfg_usb_db_type ec_cfg_usb_db_type(void);

/**
 * Check if the FW_CONFIG has enabled tablet mode operation.
 *
 * @return true if board supports tablet mode, false if the board supports
 * clamshell operation only.
 */
bool ec_cfg_has_tabletmode(void);

/**
 * Check if the FW_CONFIG has enabled keyboard backlight.
 *
 * @return true if board supports keyboard backlight, false if the board
 * doesn't support it.
 */
bool ec_cfg_has_keyboard_backlight(void);

/**
 * Check if the FW_CONFIG has enabled numeric pad.
 *
 * @return true if board supports numeric pad, false if the board
 * doesn't support it.
 */
bool ec_cfg_has_numeric_pad(void);

#endif /* __VOLTEER_CBI_EC_FW_CONFIG_H_ */
