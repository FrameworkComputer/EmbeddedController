/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "battery_smart.h"
#include "charge_state.h"
#include "console.h"
#include "driver/charger/rt946x.h"
#include "ec_commands.h"
#include "extpower.h"
#include "util.h"

static const struct battery_info info = {
	.voltage_max		= 4350,
	.voltage_normal		= 3800,
	.voltage_min		= 3000,
	.precharge_current	= 700,
	.start_charging_min_c	= 0,
	.start_charging_max_c	= 45,
	.charging_min_c		= 0,
	.charging_max_c		= 45,
	.discharging_min_c	= -20,
	.discharging_max_c	= 55,
};

const struct battery_info *battery_get_info(void)
{
	return &info;
}

int board_cut_off_battery(void)
{
	return rt946x_cutoff_battery();
}

enum battery_disconnect_state battery_get_disconnect_state(void)
{
	if (battery_is_present() == BP_YES)
		return BATTERY_NOT_DISCONNECTED;
	return BATTERY_DISCONNECTED;
}

int charger_profile_override(struct charge_state_data *curr)
{
	const struct battery_info *batt_info = battery_get_info();
	int now_discharging;

	/* battery temp in 0.1 deg C */
	int bat_temp_c = curr->batt.temperature - 2731;

	if (curr->state == ST_CHARGE) {
		/* Don't charge if outside of allowable temperature range */
		if (bat_temp_c >= batt_info->charging_max_c * 10 ||
		    bat_temp_c < batt_info->charging_min_c * 10) {
			curr->requested_current = curr->requested_voltage = 0;
			curr->batt.flags &= ~BATT_FLAG_WANT_CHARGE;
			curr->state = ST_IDLE;
			now_discharging = 0;
		/* Don't start charging if battery is nearly full */
		} else if (curr->batt.status & STATUS_FULLY_CHARGED) {
			curr->requested_current = curr->requested_voltage = 0;
			curr->batt.flags &= ~BATT_FLAG_WANT_CHARGE;
			curr->state = ST_DISCHARGE;
			now_discharging = 1;
		} else
			now_discharging = 0;
		charger_discharge_on_ac(now_discharging);
	}

	return 0;
}

/* Customs options controllable by host command. */
#define PARAM_FASTCHARGE (CS_PARAM_CUSTOM_PROFILE_MIN + 0)

enum ec_status charger_profile_override_get_param(uint32_t param,
						  uint32_t *value)
{
	return EC_RES_INVALID_PARAM;
}

enum ec_status charger_profile_override_set_param(uint32_t param,
						  uint32_t value)
{
	return EC_RES_INVALID_PARAM;
}
