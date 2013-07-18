/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery_pack.h"

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

	/*
	 * Operational temperature range
	 *   0 <= T_charge    <= 50 deg C
	 * -20 <= T_discharge <= 60 deg C
	 */
	.temp_charge_min    = CELSIUS_TO_DECI_KELVIN(0),
	.temp_charge_max    = CELSIUS_TO_DECI_KELVIN(50),
	.temp_discharge_min = CELSIUS_TO_DECI_KELVIN(-20),
	.temp_discharge_max = CELSIUS_TO_DECI_KELVIN(60),

	/* Pre-charge current: I <= 0.01C */
	.precharge_current  = 64, /* mA */
};

static inline void limit_value(int *val, int limit)
{
	if (*val > limit)
		*val = limit;
}

const struct battery_info *battery_get_info(void)
{
	return &info;
}

void battery_vendor_params(struct batt_params *batt)
{
	int *desired_current = &batt->desired_current;
	int temp_range, volt_range;

	/* Limit charging voltage */
	if (batt->desired_voltage > info.voltage_max)
		batt->desired_voltage = info.voltage_max;

	/* Don't charge if outside of allowable temperature range */
	if (batt->temperature >= info.temp_charge_max ||
	    batt->temperature <= info.temp_charge_min) {
		batt->desired_voltage = 0;
		batt->desired_current = 0;
		return;
	}

	if (batt->temperature <= CELSIUS_TO_DECI_KELVIN(10))
		temp_range = TEMP_RANGE_10;
	else if (batt->temperature <= CELSIUS_TO_DECI_KELVIN(23))
		temp_range = TEMP_RANGE_23;
	else if (batt->temperature <= CELSIUS_TO_DECI_KELVIN(35))
		temp_range = TEMP_RANGE_35;
	else if (batt->temperature <= CELSIUS_TO_DECI_KELVIN(45))
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
