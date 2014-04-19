/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"

static const struct battery_info info = {
	/*
	 * Design voltage
	 *   max    = 8.4V
	 *   normal = 7.4V
	 *   min    = 6.0V
	 */
	.voltage_max    = 8400,
	.voltage_normal = 7400,
	.voltage_min    = 6000,

	/* Pre-charge current: I <= 0.01C */
	.precharge_current  = 64, /* mA */

	/*
	 * Operational temperature range
	 *   0 <= T_charge    <= 50 deg C
	 * -20 <= T_discharge <= 60 deg C
	 */
	.start_charging_min_c = 0,
	.start_charging_max_c = 50,
	.charging_min_c       = 0,
	.charging_max_c       = 50,
	.discharging_min_c    = -20,
	.discharging_max_c    = 60,
};

const struct battery_info *battery_get_info(void)
{
	return &info;
}

#ifdef CONFIG_BATTERY_OVERRIDE_PARAMS

/*
 * Design capacity
 *   Battery capacity = 8200 mAh
 *   1C = 8200 mA
 */
#define DESIGN_CAPACITY 8200

enum {
	TEMP_RANGE_10,
	TEMP_RANGE_23,
	TEMP_RANGE_35,
	TEMP_RANGE_45,
	TEMP_RANGE_50,
	TEMP_RANGE_MAX
};

enum {
	VOLT_RANGE_7200,
	VOLT_RANGE_8000,
	VOLT_RANGE_8400,
	VOLT_RANGE_MAX
};

/*
 * Vendor provided charging method
 *      temp  : < 7.2V, 7.2V ~ 8.0V, 8.0V ~ 8.4V
 *  -  0 ~ 10 :  0.8A       1.6A         0.8A
 *  - 10 ~ 23 :  1.6A       4.0A         1.6A
 *  - 23 ~ 35 :  4.0A       4.0A         4.0A
 *  - 35 ~ 45 :  1.6A       4.0A         1.6A
 *  - 45 ~ 50 :  0.8A       1.6A         0.8A
 */
static const int const current_limit[TEMP_RANGE_MAX][VOLT_RANGE_MAX] = {
	{ 800, 1600,  800},
	{1600, 4000, 1600},
	{4000, 4000, 4000},
	{1600, 4000, 1600},
	{ 800, 1600,  800},
};

static inline void limit_value(int *val, int limit)
{
	if (*val > limit)
		*val = limit;
}

/**
 * This can override the smart battery's charging profile. On entry, all the
 * battery parameters have been updated from the smart battery. On return, the
 * desired_voltage and desired_current will be passed to the charger. To use
 * the smart battery's profile, simply do nothing.
 */
void battery_override_params(struct batt_params *batt)
{
	int *desired_current = &batt->desired_current;
	int temp_range, volt_range;
	int bat_temp_c = DECI_KELVIN_TO_CELSIUS(batt->temperature);

	/* Limit charging voltage */
	if (batt->desired_voltage > info.voltage_max)
		batt->desired_voltage = info.voltage_max;

	/* Don't charge if outside of allowable temperature range */
	if (bat_temp_c >= info.charging_max_c ||
	    bat_temp_c < info.charging_min_c) {
		batt->flags &= ~BATT_FLAG_WANT_CHARGE;
		batt->desired_voltage = 0;
		batt->desired_current = 0;
		return;
	}

	if (bat_temp_c <= 10)
		temp_range = TEMP_RANGE_10;
	else if (bat_temp_c <= 23)
		temp_range = TEMP_RANGE_23;
	else if (bat_temp_c <= 35)
		temp_range = TEMP_RANGE_35;
	else if (bat_temp_c <= 45)
		temp_range = TEMP_RANGE_45;
	else
		temp_range = TEMP_RANGE_50;

	if (batt->voltage < 7200)
		volt_range = VOLT_RANGE_7200;
	else if (batt->voltage < 8000)
		volt_range = VOLT_RANGE_8000;
	else
		volt_range = VOLT_RANGE_8400;

	limit_value(desired_current, current_limit[temp_range][volt_range]);

	/* If battery wants current, give it at least the precharge current */
	if (*desired_current > 0 && *desired_current < info.precharge_current)
		*desired_current = info.precharge_current;
}

#endif	/* CONFIG_BATTERY_OVERRIDE_PARAMS */
