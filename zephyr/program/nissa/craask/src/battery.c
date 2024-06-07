/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "charge_state.h"
#include "hooks.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

#define CHECK_BATT_STAT_DELAY_MS (500 * MSEC)
static int check_batt_retry;

void board_check_battery_status(void)
{
	enum battery_disconnect_state battery_disconnect_status =
		battery_get_disconnect_state();

	/*
	 * The following 2 states can read DFET status successfully
	 * so not need to do initialize battery type again.
	 * BATTERY_DISCONNECTED: The DFET is off.
	 * BATTERY_NOT_DISCONNECTED: The battery can discharge.
	 * Therefore, do initilialize only at BATTERY_DISCONNECT_ERROR.
	 */
	if (battery_disconnect_status != BATTERY_DISCONNECT_ERROR) {
		check_batt_retry = 0;
		return;
	}

	check_batt_retry++;
	if (check_batt_retry > 5) {
		LOG_INF("Board has retried init_battery_type 5 times.");
		check_batt_retry = 0;
		return;
	}

	LOG_INF("Retry init_battery_type: %d", check_batt_retry);
	init_battery_type();
}
DECLARE_DEFERRED(board_check_battery_status);

__override int board_get_default_battery_type(void)
{
	const struct batt_params *batt = charger_current_battery_params();

	if (batt->flags & BATT_FLAG_RESPONSIVE) {
		/* Check Battery status again after 500msec. */
		hook_call_deferred(&board_check_battery_status_data,
				   CHECK_BATT_STAT_DELAY_MS);
	} else {
		check_batt_retry = 0;
	}

	return DEFAULT_BATTERY_TYPE;
}
