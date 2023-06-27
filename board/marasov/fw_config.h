/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_MARASOV_FW_CONFIG_H_
#define __BOARD_MARASOV_FW_CONFIG_H_

#include <stdint.h>

/****************************************************************************
 * CBI FW_CONFIG layout for marasov board.
 *
 * Source of truth is the project/brya/marasov/config.star configuration file.
 */

enum ec_cfg_keyboard_backlight_type {
	KEYBOARD_BACKLIGHT_DISABLED = 0,
	KEYBOARD_BACKLIGHT_ENABLED = 1
};

union marasov_cbi_fw_config {
	struct {
		enum ec_cfg_keyboard_backlight_type kb_bl : 1;
		uint32_t audio : 3;
		uint32_t ufc : 1;
		uint32_t reserved_1 : 25;
		uint32_t storage : 2;
	};
	uint32_t raw_value;
};

/**
 * Read the cached FW_CONFIG.  Guaranteed to have valid values.
 *
 * @return the FW_CONFIG for the board.
 */
union marasov_cbi_fw_config get_fw_config(void);

/**
 * Get the keyboard backlight type from FW_CONFIG.
 *
 * @return the keyboard backlight type.
 */
enum ec_cfg_keyboard_backlight_type ec_cfg_kb_bl_type(void);

#endif /* __BOARD_MARASOV_FW_CONFIG_H_ */
