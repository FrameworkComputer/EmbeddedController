/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"
#include "charger_chips.h"
#include "common.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/charger/isl923x.h"
#include "extpower.h"
#include "hooks.h"

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(alt_charger, LOG_LEVEL_INF);

static void alt_charger_init(void)
{
	int ret;
	uint32_t val;

	ret = cbi_get_board_version(&val);
	if (ret == EC_SUCCESS && val >= 2) {
		LOG_INF("Enable alternate charger ISL9238C.");
		CHG_ENABLE_ALTERNATE(0);
	}
}
DECLARE_HOOK(HOOK_INIT, alt_charger_init, HOOK_PRIO_POST_FIRST);
