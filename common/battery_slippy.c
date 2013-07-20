/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery_pack.h"
#include "gpio.h"

/* FIXME: We need REAL values for all this stuff */
static const struct battery_info info = {

	.voltage_max    = 16800,
	.voltage_normal = 14800,
	.voltage_min    = 10800,

	/*
	 * Operational temperature range
	 *   0 <= T_charge    <= 50 deg C
	 * -20 <= T_discharge <= 60 deg C
	 */
	.temp_charge_min    = CELSIUS_TO_DECI_KELVIN(0),
	.temp_charge_max    = CELSIUS_TO_DECI_KELVIN(50),
	.temp_discharge_min = CELSIUS_TO_DECI_KELVIN(-20),
	.temp_discharge_max = CELSIUS_TO_DECI_KELVIN(60),

	/* Pre-charge values. */
	.precharge_current  = 256,	/* mA */
};

const struct battery_info *battery_get_info(void)
{
	return &info;
}

/* FIXME: The smart battery should do the right thing - that's why it's
 * called "smart". Do we really want to second-guess it? For now, let's not. */
void battery_vendor_params(struct batt_params *batt)
{
#if 0
	/* Limit charging voltage */
	if (batt->desired_voltage > info.voltage_max)
		batt->desired_voltage = info.voltage_max;

	/* Don't charge if outside of allowable temperature range */
	if (batt->temperature >= info.temp_charge_max ||
	    batt->temperature <= info.temp_charge_min) {
		batt->desired_voltage = 0;
		batt->desired_current = 0;
	}
#endif
}

/**
 * Physical detection of battery connection.
 */
int battery_is_connected(void)
{
	return (gpio_get_level(GPIO_BAT_DETECT_L) == 0);
}
