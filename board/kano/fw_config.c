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

static union kano_cbi_fw_config fw_config;
BUILD_ASSERT(sizeof(fw_config) == sizeof(uint32_t));

/*
 * FW_CONFIG defaults for kano if the CBI.FW_CONFIG data is not
 * initialized.
 */
static const union kano_cbi_fw_config fw_config_defaults = {
	.kb_bl = KEYBOARD_BACKLIGHT_ENABLED,
};

/****************************************************************************
 * Kano FW_CONFIG access
 */
void board_init_fw_config(void)
{
	if (cbi_get_fw_config(&fw_config.raw_value)) {
		CPRINTS("CBI: Read FW_CONFIG failed, using board defaults");
		fw_config = fw_config_defaults;
	}

	if (get_board_id() == 0) {
		/*
		 * Early boards have a zero'd out FW_CONFIG, so replace
		 * it with a sensible default value.
		 */
		if (fw_config.raw_value == 0) {
			CPRINTS("CBI: FW_CONFIG is zero, using board defaults");
			fw_config = fw_config_defaults;
		}
	}
}

union kano_cbi_fw_config get_fw_config(void)
{
	return fw_config;
}

bool ec_cfg_has_kblight(void)
{
	return (fw_config.kb_bl == KEYBOARD_BACKLIGHT_ENABLED);
}
