/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Features common to ECOS and Zephyr */
#include "common.h"
#include "cbi.h"
#include "cbi_ec_fw_config.h"
#include "keyboard_raw.h"
#include "usbc_config.h"

/******************************************************************************/
/*
 * FW_CONFIG defaults for Volteer if the CBI data is not initialized.
 */
union volteer_cbi_fw_config fw_config_defaults = {
	.usb_db = DB_USB4_GEN2,
};

__override void board_cbi_init(void)
{
	config_usb3_db_type();
	if ((!IS_ENABLED(TEST_BUILD) && !ec_cfg_has_numeric_pad()) ||
	    get_board_id() <= 2)
		keyboard_raw_set_cols(KEYBOARD_COLS_NO_KEYPAD);
}
