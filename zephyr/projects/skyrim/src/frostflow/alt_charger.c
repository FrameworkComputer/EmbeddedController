/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include "charger_chips.h"
#include "common.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "hooks.h"

LOG_MODULE_DECLARE(frostflow, CONFIG_SKYRIM_LOG_LEVEL);

static void alt_charger_init(void)
{
	int ret;
	uint32_t val;

	ret = cros_cbi_get_fw_config(FW_CHARGER, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_CHARGER);
		return;
	}
}
DECLARE_HOOK(HOOK_INIT, alt_charger_init, HOOK_PRIO_POST_FIRST);
