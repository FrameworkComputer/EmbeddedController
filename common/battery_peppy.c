/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery_pack.h"
#include "gpio.h"
#include "host_command.h"
#include "smart_battery.h"

#define SB_SHIP_MODE_ADDR	0x3a
#define SB_SHIP_MODE_DATA	0xc574

/* Values for 54Wh 3UPF656790-1-T1001 battery */
static const struct battery_info info = {

	.voltage_max    = 12600,
	.voltage_normal = 11100, /* Average of max & min */
	.voltage_min    =  9000,

	/*
	 * Operational temperature range
	 * 0 <= T_charge    <= 60 deg C
	 * 0 <= T_discharge <= 50 deg C
	 */
	.temp_charge_min    = CELSIUS_TO_DECI_KELVIN(0),
	.temp_charge_max    = CELSIUS_TO_DECI_KELVIN(60),
	.temp_discharge_min = CELSIUS_TO_DECI_KELVIN(0),
	.temp_discharge_max = CELSIUS_TO_DECI_KELVIN(50),

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

int battery_command_cut_off(struct host_cmd_handler_args *args)
{
	return sb_write(SB_SHIP_MODE_ADDR, SB_SHIP_MODE_DATA);
}
DECLARE_HOST_COMMAND(EC_CMD_BATTERY_CUT_OFF, battery_command_cut_off,
		     EC_VER_MASK(0));

/**
 * Physical detection of battery connection.
 */
int battery_is_connected(void)
{
	return (gpio_get_level(GPIO_BAT_DETECT_L) == 0);
}
