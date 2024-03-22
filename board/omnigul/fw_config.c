/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cbi.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "cros_board_info.h"
#include "fw_config.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

static union omnigul_cbi_fw_config fw_config;
BUILD_ASSERT(sizeof(fw_config) == sizeof(uint32_t));

/*
 * FW_CONFIG defaults for Omnigul if the CBI.FW_CONFIG data is not
 * initialized.
 */
static const union omnigul_cbi_fw_config fw_config_defaults = {
	.kb_bl = KEYBOARD_BACKLIGHT_ENABLED,
	.tab_mode = TABLETMODE_DISABLED,
	.aud = AUDIO_ALC5682I_ALC1019,
	.sar_id = SAR_ID_0,
};

/****************************************************************************
 * Omnigul FW_CONFIG access
 */
void board_init_fw_config(void)
{
	if (cbi_get_fw_config(&fw_config.raw_value)) {
		CPRINTS("CBI: Read FW_CONFIG failed, using board defaults");
		fw_config = fw_config_defaults;
	}
}

union omnigul_cbi_fw_config get_fw_config(void)
{
	return fw_config;
}

bool ec_cfg_has_tabletmode(void)
{
	return (fw_config.tab_mode == TABLETMODE_ENABLED);
}

bool ec_cfg_has_keyboard_numpad(void)
{
	return (fw_config.kb_numpd == KEYBOARD_NUMPAD_PRESENT);
}

enum ec_cfg_keyboard_layout ec_cfg_keyboard_layout(void)
{
	return fw_config.kb_layout;
}
