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
#include "driver/battery/max17055.h"
#include "driver/charger/rt946x.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "util.h"

static uint8_t batt_id = 0xff;

/* Do not change the enum values. We directly use strap gpio level to index. */
enum battery_type {
	BATTERY_SIMPLO = 0,
	BATTERY_AETECH,
	BATTERY_COUNT
};

static const struct battery_info info[] = {
	[BATTERY_SIMPLO] = {
		.voltage_max		= 4400,
		.voltage_normal		= 3840,
		.voltage_min		= 3000,
		.precharge_current	= 256,
		.start_charging_min_c	= 0,
		.start_charging_max_c	= 45,
		.charging_min_c		= 0,
		.charging_max_c		= 60,
		.discharging_min_c	= -20,
		.discharging_max_c	= 60,
	},
	[BATTERY_AETECH] = {
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
	}
};

static const struct max17055_batt_profile batt_profile[] = {
	/*
	 * TODO(philipchen): Update the battery profile for Simplo
	 * battery once we have the characterization result.
	 */
	[BATTERY_SIMPLO] = {
		.is_ez_config		= 1,
		.design_cap		= MAX17055_DESIGNCAP_REG(9120),
		.ichg_term		= MAX17055_ICHGTERM_REG(180),
		.v_empty_detect		= MAX17055_VEMPTY_REG(2700, 3280),
	},
	[BATTERY_AETECH] = {
		.is_ez_config		= 0,
		.design_cap		= 0x232f, /* 9007mAh */
		.ichg_term		= 0x0240, /* 180mA */
		/* Empty voltage = 2700mV, Recovery voltage = 3280mV */
		.v_empty_detect		= 0x8752,
		.learn_cfg		= 0x4476,
		.dpacc			= 0x0c7b,
		.rcomp0			= 0x0077,
		.tempco			= 0x1d3f,
		.qr_table00		= 0x1200,
		.qr_table10		= 0x0900,
		.qr_table20		= 0x0480,
		.qr_table30		= 0x0480,
	},
};

const struct battery_info *battery_get_info(void)
{
	if (batt_id >= BATTERY_COUNT)
		batt_id = gpio_get_level(GPIO_BATT_ID);

	return &info[batt_id];
}

const struct max17055_batt_profile *max17055_get_batt_profile(void)
{
	if (batt_id >= BATTERY_COUNT)
		batt_id = gpio_get_level(GPIO_BATT_ID);

	return &batt_profile[batt_id];
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
