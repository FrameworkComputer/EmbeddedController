/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _DEDEDE_CBI_FW_CONFIG__H_
#define _DEDEDE_CBI_FW_CONFIG__H_

/****************************************************************************
 * Dedede CBI FW Configuration
 */

/*
 * Daughter Board (Bits 0-3)
 */
enum fw_config_db {
	DB_NONE,
	DB_2C,
	DB_1C_LTE,
	DB_1A_HDMI,
	DB_1C_1A,
	DB_LTE_HDMI,
	DB_1C_1A_LTE,
	DB_1C,
	DB_1A_HDMI_LTE,
};
#define FW_CONFIG_DB_OFFSET 0
#define FW_CONFIG_DB_MASK GENMASK(3, 0)

/*
 * Stylus (1 bit)
 */
enum fw_config_stylus {
	STYLUS_ABSENT = 0,
	STYLUS_PRESENT = 1,
};
#define FW_CONFIG_STYLUS_OFFSET 4
#define FW_CONFIG_STYLUS_MASK GENMASK(4, 4)

/*
 * Keyboard backlight (1 bit)
 */
enum fw_config_kblight_type {
	KB_BL_ABSENT = 0,
	KB_BL_PRESENT = 1,
};
#define FW_CONFIG_KB_BL_OFFSET 8
#define FW_CONFIG_KB_BL_MASK GENMASK(8, 8)

/*
 * Keyboard numeric pad (1 bit)
 */
enum fw_config_numeric_pad_type {
	NUMERIC_PAD_ABSENT = 0,
	NUMERIC_PAD_PRESENT = 1,
};
#define FW_CONFIG_KB_NUMPAD_OFFSET 9
#define FW_CONFIG_KB_NUMPAD_MASK GENMASK(9, 9)

/*
 * Tablet Mode (1 bit)
 */
enum fw_config_tablet_mode_type {
	TABLET_MODE_ABSENT = 0,
	TABLET_MODE_PRESENT = 1,
};
#define FW_CONFIG_TABLET_MODE_OFFSET 10
#define FW_CONFIG_TABLET_MODE_MASK GENMASK(10, 10)

#define FW_CONFIG_KB_LAYOUT_OFFSET 12
#define FW_CONFIG_KB_LAYOUT_MASK GENMASK(13, 12)

/*
 * Hdmi (1 bit)
 */
enum fw_config_hdmi_type {
	HDMI_ABSENT = 0,
	HDMI_PRESENT = 1,
};
#define FW_CONFIG_HDMI_OFFSET 17
#define FW_CONFIG_HDMI_MASK GENMASK(17, 17)

/*
 * BC12 (1 bit)
 */
enum fw_config_bc12 {
	BC12_SUPPORT = 0,
	BC12_NONE = 1,
};
#define FW_CONFIG_BC12_SUPPORT 20
#define FW_CONFIG_BC12_MASK GENMASK(20, 20)

enum fw_config_db get_cbi_fw_config_db(void);
enum fw_config_stylus get_cbi_fw_config_stylus(void);
enum fw_config_kblight_type get_cbi_fw_config_kblight(void);
enum fw_config_tablet_mode_type get_cbi_fw_config_tablet_mode(void);
enum fw_config_numeric_pad_type get_cbi_fw_config_numeric_pad(void);
enum fw_config_hdmi_type get_cbi_fw_config_hdmi(void);

int get_cbi_fw_config_keyboard(void);
int get_cbi_fw_config_bc_support(void);

#endif /* _DEDEDE_CBI_FW_CONFIG__H_ */
