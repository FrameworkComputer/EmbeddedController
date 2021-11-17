/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_KANO_FW_CONFIG_H_
#define __BOARD_KANO_FW_CONFIG_H_

#include <stdint.h>

/****************************************************************************
 * CBI FW_CONFIG layout for Kano board.
 *
 * Source of truth is the project/brya/kano/config.star configuration file.
 */

enum ec_cfg_keyboard_backlight_type {
	KEYBOARD_BACKLIGHT_DISABLED = 0,
	KEYBOARD_BACKLIGHT_ENABLED = 1
};

union kano_cbi_fw_config {
	struct {
		enum ec_cfg_keyboard_backlight_type	kb_bl : 1;
		uint32_t				audio : 3;
		uint32_t				ufc : 2;
		uint32_t				reserved_1 : 26;
	};
	uint32_t raw_value;
};

/**
 * Read the cached FW_CONFIG.  Guaranteed to have valid values.
 *
 * @return the FW_CONFIG for the board.
 */
union kano_cbi_fw_config get_fw_config(void);

/**
 * Check if the FW_CONFIG has enabled keyboard backlight.
 *
 * @return true if board supports keyboard backlight, false if the board
 * doesn't support it.
 */
bool ec_cfg_has_kblight(void);

#endif /* __BOARD_KANO_FW_CONFIG_H_ */
