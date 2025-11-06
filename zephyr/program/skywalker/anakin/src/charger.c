/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "console.h"
#include "extpower.h"
#include "hooks.h"
#include "usb_common.h"
#include "zephyr/include/usbc/pdc_power_mgmt.h"

#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <ap_power/ap_power.h>

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

#define MAX_CURRENT 2250
#define MAX_VOLTAGE 20000

#define INT_RECHECK_US (1000 * USEC_PER_MSEC)

static void board_set_charge_limit_current(void)
{
	uint32_t rdo = 0;

	if (extpower_is_present()) {
		int port = charge_manager_get_active_charge_port();

		const uint32_t *const src_caps =
			pdc_power_mgmt_get_src_caps(port);

		pdc_power_mgmt_get_rdo(port, &rdo);

		uint32_t src_cap = src_caps[(RDO_POS(rdo) - 1)];
		uint32_t max_ma = 0, max_mv = 0, min_mv = 0;

		pd_extract_pdo_power_unclamped(src_cap, &max_ma, &max_mv,
					       &min_mv);

		if (max_mv >= MAX_VOLTAGE && max_ma >= MAX_CURRENT) {
			charge_set_input_current_limit(MAX_CURRENT, max_mv);
			CPRINTS("The charging current is limited to 2.25A");
		}
	}
}
DECLARE_DEFERRED(board_set_charge_limit_current);

static void chagrge_limit_current_init(void)
{
	hook_call_deferred(&board_set_charge_limit_current_data,
			   INT_RECHECK_US);
}
DECLARE_HOOK(HOOK_INIT, chagrge_limit_current_init, HOOK_PRIO_LAST);
DECLARE_HOOK(HOOK_POWER_SUPPLY_CHANGE, chagrge_limit_current_init,
	     HOOK_PRIO_LAST);
