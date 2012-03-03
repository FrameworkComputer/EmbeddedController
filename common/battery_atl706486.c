/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile for ATL706486
 */

#include "battery_pack.h"

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

/* Vendor provided parameters for battery charging */
void battery_vendor_params(struct batt_params *batt)
{
	/* Designed capacity
	 *   Battery capacity = 8400 mAh
	 *   1C = 8400 mA
	 */
	const int C = 8400;
	const int C_01 = C * 0.1;
	const int C_02 = C * 0.2;
	const int C_05 = C * 0.5;
	const int C_07 = C * 0.7;

	/* Designed voltage
	 *   max    = 8.4V
	 *   normal = 7.4V
	 */
	const int V_max = 8400;

	/* Operation temperation range
	 *   0   <= T_charge    <= 45
	 *   -20 <= T_discharge <= 60
	 */
	const int T_charge_min = 0;
	const int T_charge_max = 45;

	int *desired_current = &batt->desired_current;

	/* Hard limits
	 *  - charging voltage < 8.4V
	 *  - charging temperature range 0 ~ 45 degree Celcius
	 *  */
	if (batt->desired_voltage > V_max)
		batt->desired_voltage = V_max;
	if (batt->temperature >= celsius_to_deci_kelvin(T_charge_max) ||
	    batt->temperature <= celsius_to_deci_kelvin(T_charge_min)) {
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


