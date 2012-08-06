/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery_pack.h"

/* Design capacity
 *   Battery capacity = 8200 mAh
 *   1C = 8200 mA
 */
#define C     8200
#define C_001 (int)(C * 0.01)
/*
 * Common charging currents:
 *   #define C_01  (int)(C * 0.1) ==  820mA
 *   #define C_02  (int)(C * 0.2) == 1640mA
 *   #define C_05  (int)(C * 0.5) == 4100mA
 *   #define C_07  (int)(C * 0.7) == 5740mA
 */

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

/* Vendor provided charging method
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
	/* Designed voltage
	 *   max    = 8.4V
	 *   normal = 7.4V
	 *   min    = 6.0V
	 */
	.voltage_max    = 8400,
	.voltage_normal = 7400,
	.voltage_min    = 6000,

	/* Operation temperation range
	 *   0   <= T_charge    <= 50 deg C
	 *   -20 <= T_discharge <= 60 deg C
	 *
	 * The temperature values below should be deci-Kelvin
	 */
	.temp_charge_min    =   0 * 10 + 2731,
	.temp_charge_max    =  50 * 10 + 2731,
	.temp_discharge_min = -20 * 10 + 2731,
	.temp_discharge_max =  60 * 10 + 2731,

	/* Pre-charge voltage and current
	 *   I <= 0.01C
	 */
	.precharge_current  = C_001,
};

/* Convert Celsius degree to Deci Kelvin degree */
static inline int celsius_to_deci_kelvin(int degree_c)
{
	return degree_c * 10 + 2731;
}

static inline void limit_value(int *val, int limit)
{
	if (*val > limit)
		*val = limit;
}

const struct battery_info *battery_get_info(void)
{
	return &info;
}

/* Vendor provided parameters for battery charging */
void battery_vendor_params(struct batt_params *batt)
{
	int *desired_current = &batt->desired_current;
	int temp_range, volt_range;

	/* Hard limits
	 *  - charging voltage < 8.4V
	 *  - charging temperature range 0 ~ 45 degree Celcius
	 *  */
	if (batt->desired_voltage > info.voltage_max)
		batt->desired_voltage = info.voltage_max;
	if (batt->temperature >= info.temp_charge_max ||
	    batt->temperature <= info.temp_charge_min) {
		batt->desired_voltage = 0;
		batt->desired_current = 0;
		return;
	}

	if (batt->temperature <= celsius_to_deci_kelvin(10))
		temp_range = TEMP_RANGE_10;
	else if (batt->temperature <= celsius_to_deci_kelvin(23))
		temp_range = TEMP_RANGE_23;
	else if (batt->temperature <= celsius_to_deci_kelvin(35))
		temp_range = TEMP_RANGE_35;
	else if (batt->temperature <= celsius_to_deci_kelvin(45))
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

#ifndef CONFIG_SLOW_PRECHARGE
	/* Trickle charging and pre-charging current should be 0.01 C */
	if (*desired_current < info.precharge_current)
		*desired_current = info.precharge_current;
#endif /* CONFIG_SLOW_PRECHARGE */

}


