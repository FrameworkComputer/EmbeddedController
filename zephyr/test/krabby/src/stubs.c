/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_ramp.h"
#include "charge_state.h"

int board_set_active_charge_port(int port)
{
	return 0;
}

int board_is_vbus_too_low(int port, enum chg_ramp_vbus_state ramp_state)
{
	return 0;
}

void board_set_charge_limit(int port, int supplier, int charge_ma, int max_ma,
			    int charge_mv)
{
}

const struct batt_params *charger_current_battery_params(void)
{
	static const struct batt_params params = {};

	return &params;
}
