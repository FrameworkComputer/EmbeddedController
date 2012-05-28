/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile for ATL706486
 */

#include "battery_pack.h"

/* Design capacity
 *   Battery capacity = 8500 mAh
 *   1C = 8500 mA
 */
#define C     8500
#define C_001 (int)(C * 0.01)
#define C_01  (int)(C * 0.1)
#define C_02  (int)(C * 0.2)
#define C_05  (int)(C * 0.5)
#define C_07  (int)(C * 0.7)

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
	 *   0   <= T_charge    <= 45 deg C
	 *   -20 <= T_discharge <= 60 deg C
	 *
	 * The temperature values below should be deci-Kelvin
	 */
	.temp_charge_min    =   0,
	.temp_charge_max    =  45 * 10 + 2731,
	.temp_discharge_min = -20 * 10 + 2731,
	.temp_discharge_max =  60 * 10 + 2731,

	/* Maximum discharging current
	 * TODO(rong): Check if we need this in power manager
	 *
	 * 1.0C
	 * 25 <= T_maxdischarge <= 45
	 *
	 * .current_discharge_max = C,
	 * .temp_maxdisch_max     = 45,
	 * .temp_maxdisch_min     = 25
	 *
	 */

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

	/* Hard limits
	 *  - charging voltage < 8.4V
	 *  - charging temperature range 0 ~ 45 degree Celcius
	 *  */
	if (batt->desired_voltage > info.voltage_max)
		batt->desired_voltage = info.voltage_max;
	if (batt->temperature >= celsius_to_deci_kelvin(info.temp_charge_max) ||
	    batt->temperature <= celsius_to_deci_kelvin(info.temp_charge_min)) {
		batt->desired_voltage = 0;
		batt->desired_current = 0;
	}

	/* Vendor provided charging method
	 *      temp  :    I - V   ,   I - V
	 *  -  0 ~ 10 : 0.2C - 8.0V, 0.1C to 8.4V
	 *  - 10 ~ 23 : 0.5C - 8.0V, 0.2C to 8.4V
	 *  - 23 ~ 45 : 0.7C - 8.0V, 0.2C to 8.4V
	 */
	if (batt->temperature <= celsius_to_deci_kelvin(10)) {
		if (batt->voltage < 8000)
			limit_value(desired_current, C_02);
		else
			limit_value(desired_current, C_01);
	} else if (batt->temperature <= celsius_to_deci_kelvin(23)) {
		if (batt->voltage < 8000)
			limit_value(desired_current, C_05);
		else
			limit_value(desired_current, C_02);
	} else {
		if (batt->voltage < 8000)
			limit_value(desired_current, C_07);
		else
			limit_value(desired_current, C_02);
	}
}


