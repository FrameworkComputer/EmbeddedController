/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _DOJO_CBI_FW_CONFIG__H_
#define _DOJO_CBI_FW_CONFIG__H_

/****************************************************************************
 * Dojo CBI FW Configuration
 */

/*
 * Keyboard backlight (bit 0)
 */
enum fw_config_kblight_type {
	KB_BL_ABSENT = 0,
	KB_BL_PRESENT = 1,
};
#define FW_CONFIG_KB_BL_OFFSET			0
#define FW_CONFIG_KB_BL_MASK			GENMASK(0, 0)

/*
 * Keyboard layout (bit 4-5)
 */
enum fw_config_kblayout_type {
	KB_BL_TOGGLE_KEY_ABSENT = 0, /* Vol-up key on T12 */
	KB_BL_TOGGLE_KEY_PRESENT = 1, /* Vol-up key on T13 */
};
#define FW_CONFIG_KB_LAYOUT_OFFSET		4
#define FW_CONFIG_KB_LAYOUT_MASK		GENMASK(5, 4)

enum fw_config_kblight_type get_cbi_fw_config_kblight(void);
enum fw_config_kblayout_type get_cbi_fw_config_kblayout(void);

#endif /* _DOJO_CBI_FW_CONFIG__H_ */
