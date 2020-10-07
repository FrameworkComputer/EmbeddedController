/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "cbi_ec_fw_config.h"
#include "cros_board_info.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

static union volteer_cbi_fw_config fw_config;
BUILD_ASSERT(sizeof(fw_config) == sizeof(uint32_t));

/****************************************************************************
 * Volteer FW_CONFIG access
 */
void init_fw_config(void)
{
	if (cbi_get_fw_config(&fw_config.raw_value)) {
		CPRINTS("CBI: Read FW_CONFIG failed, using board defaults");
		fw_config = fw_config_defaults;
	}
}

union volteer_cbi_fw_config get_fw_config(void)
{
	return fw_config;
}

enum ec_cfg_usb_db_type ec_cfg_usb_db_type(void)
{
	return fw_config.usb_db;
}

bool ec_cfg_has_tabletmode(void)
{
	return (fw_config.tabletmode == TABLETMODE_ENABLED);
}

bool ec_cfg_has_keyboard_backlight(void)
{
	return (fw_config.kb_bl == KEYBOARD_BACKLIGHT_ENABLED);
}

bool ec_cfg_has_numeric_pad(void)
{
	return (fw_config.num_pad == NUMERIC_PAD_ENABLED);
}

