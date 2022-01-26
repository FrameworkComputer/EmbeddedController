/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_BRYA_FW_CONFIG_H_
#define __BOARD_BRYA_FW_CONFIG_H_

#include <stdint.h>

/****************************************************************************
 * CBI FW_CONFIG layout for Banshee board.
 *
 * Source of truth is the project/brya/brya/config.star configuration file.
 */


enum ec_cfg_keyboard_backlight_type {
	KEYBOARD_BACKLIGHT_DISABLED = 0,
	KEYBOARD_BACKLIGHT_ENABLED = 1
};

union banshee_cbi_fw_config {
	struct {
		uint32_t				sd_db : 2;
		uint32_t				lte_db : 1;
		enum ec_cfg_keyboard_backlight_type	kb_bl : 1;
		uint32_t				audio : 3;
		uint32_t				reserved_1 : 21;
	};
	uint32_t raw_value;
};

/**
 * Read the cached FW_CONFIG.  Guaranteed to have valid values.
 *
 * @return the FW_CONFIG for the board.
 */
union banshee_cbi_fw_config get_fw_config(void);

#endif /* __BOARD_BRYA_FW_CONFIG_H_ */
