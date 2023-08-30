/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger_chips.h"
#include "common.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "hooks.h"
#include "ztest/alt_charger.h"

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(skyrim, CONFIG_SKYRIM_LOG_LEVEL);

void alt_charger_init(void)
{
	int ret;
	uint32_t val;

	ret = cros_cbi_get_fw_config(FW_CHARGER, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_CHARGER);
		return;
	}

	if (val == FW_CHARGER_ISL9538)
		CHG_ENABLE_ALTERNATE(0);
}
DECLARE_HOOK(HOOK_INIT, alt_charger_init, HOOK_PRIO_POST_FIRST);
