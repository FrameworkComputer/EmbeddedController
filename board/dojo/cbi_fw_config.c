/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cbi_fw_config.h"
#include "common.h"
#include "console.h"
#include "cros_board_info.h"
#include "hooks.h"

/****************************************************************************
 * Dojo CBI FW Configuration
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

enum fw_config_kblight_type get_cbi_fw_config_kblight(void)
{
	return ((cached_fw_config & FW_CONFIG_KB_BL_MASK) >>
		FW_CONFIG_KB_BL_OFFSET);
}

enum fw_config_kblayout_type get_cbi_fw_config_kblayout(void)
{
	return ((cached_fw_config & FW_CONFIG_KB_LAYOUT_MASK) >>
		FW_CONFIG_KB_LAYOUT_OFFSET);
}
