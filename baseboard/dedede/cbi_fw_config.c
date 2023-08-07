/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cbi_fw_config.h"
#include "common.h"
#include "console.h"
#include "cros_board_info.h"
#include "hooks.h"

/****************************************************************************
 * Dedede CBI FW Configuration
 */

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

/* Cache FW_CONFIG on init since we don't expect it to change in runtime */
static uint32_t cached_fw_config;

static void cbi_fw_config_init(void)
{
	if (cbi_get_fw_config(&cached_fw_config) != EC_SUCCESS)
		/* Default to 0 when CBI isn't populated */
		cached_fw_config = 0;

	CPRINTS("FW_CONFIG: 0x%04X", cached_fw_config);
}
DECLARE_HOOK(HOOK_INIT, cbi_fw_config_init, HOOK_PRIO_FIRST);

enum fw_config_db get_cbi_fw_config_db(void)
{
	return ((cached_fw_config & FW_CONFIG_DB_MASK) >> FW_CONFIG_DB_OFFSET);
}

enum fw_config_stylus get_cbi_fw_config_stylus(void)
{
	return ((cached_fw_config & FW_CONFIG_STYLUS_MASK) >>
		FW_CONFIG_STYLUS_OFFSET);
}

enum fw_config_kblight_type get_cbi_fw_config_kblight(void)
{
	return ((cached_fw_config & FW_CONFIG_KB_BL_MASK) >>
		FW_CONFIG_KB_BL_OFFSET);
}

enum fw_config_tablet_mode_type get_cbi_fw_config_tablet_mode(void)
{
	return ((cached_fw_config & FW_CONFIG_TABLET_MODE_MASK) >>
		FW_CONFIG_TABLET_MODE_OFFSET);
}

int get_cbi_fw_config_keyboard(void)
{
	return ((cached_fw_config & FW_CONFIG_KB_LAYOUT_MASK) >>
		FW_CONFIG_KB_LAYOUT_OFFSET);
}

enum fw_config_numeric_pad_type get_cbi_fw_config_numeric_pad(void)
{
	return ((cached_fw_config & FW_CONFIG_KB_NUMPAD_MASK) >>
		FW_CONFIG_KB_NUMPAD_OFFSET);
}

enum fw_config_hdmi_type get_cbi_fw_config_hdmi(void)
{
	return ((cached_fw_config & FW_CONFIG_HDMI_MASK) >>
		FW_CONFIG_HDMI_OFFSET);
}

int get_cbi_fw_config_bc_support(void)
{
	return ((cached_fw_config & FW_CONFIG_BC12_MASK) >>
		FW_CONFIG_BC12_SUPPORT);
}
