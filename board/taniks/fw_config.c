/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "cros_board_info.h"
#include "fw_config.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

static union taniks_cbi_fw_config fw_config;
BUILD_ASSERT(sizeof(fw_config) == sizeof(uint32_t));

/*
 * FW_CONFIG defaults for Taniks if the CBI.FW_CONFIG data is not
 * initialized.
 */
static const union taniks_cbi_fw_config fw_config_defaults = {
	.usb_db = DB_USB3_PS8815,
	.kb_bl = KEYBOARD_BACKLIGHT_ENABLED,
};

/****************************************************************************
 * Taniks FW_CONFIG access
 */
void board_init_fw_config(void)
{
	if (cbi_get_fw_config(&fw_config.raw_value)) {
		CPRINTS("CBI: Read FW_CONFIG failed, using board defaults");
		fw_config = fw_config_defaults;
	}

	if (get_board_id() == 0) {
		/* TODO(b/211076082): Update CBI fw config structure
		 * Update correct FW_CONFIG.
		 */
		CPRINTS("CBI: Using board defaults for early board");
		if (ec_cfg_has_tabletmode()) {
			fw_config = fw_config_defaults;
		} 
	}
}

union taniks_cbi_fw_config get_fw_config(void)
{
	return fw_config;
}

enum ec_cfg_usb_db_type ec_cfg_usb_db_type(void)
{
	return fw_config.usb_db;
}

bool ec_cfg_has_keyboard_backlight(void)
{
	return (fw_config.kb_bl == KEYBOARD_BACKLIGHT_ENABLED);
}

bool ec_cfg_has_tabletmode(void)
{
	return (fw_config.tabletmode == TABLETMODE_ENABLED);
}
