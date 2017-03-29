/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */
#include "battery.h"
#include "battery_smart.h"
#include "extpower.h"
#include "gpio.h"

/* Shutdown mode parameter to write to manufacturer access register */
#define SB_SHUTDOWN_DATA	0x0010

static enum battery_present batt_pres_prev = BP_NOT_SURE;

static const struct battery_info info = {
	.voltage_max = 13200,/* mV */
	.voltage_normal = 11400,
	.voltage_min = 9000,
	.precharge_current = 256,/* mA */
	.start_charging_min_c = 0,
	.start_charging_max_c = 50,
	.charging_min_c = 0,
	.charging_max_c = 60,
	.discharging_min_c = -20,
	.discharging_max_c = 70,
};
const struct battery_info *battery_get_info(void)
{
	return &info;
}
int board_cut_off_battery(void)
{
	int rv;
	/* Ship mode command must be sent twice to take effect */
	rv = sb_write(SB_MANUFACTURER_ACCESS, SB_SHUTDOWN_DATA);
	if (rv != EC_SUCCESS)
		return rv;
	return sb_write(SB_MANUFACTURER_ACCESS, SB_SHUTDOWN_DATA);
}

static inline enum battery_present battery_hw_present(void)
{
	/* The GPIO is low when the battery is physically present */
	return gpio_get_level(GPIO_EC_BATT_PRES_L) ? BP_NO : BP_YES;
}

static int battery_init(void)
{
	int batt_status;

	return battery_status(&batt_status) ? 0 :
		!!(batt_status & STATUS_INITIALIZED);
}

/*
 * Physical detection of battery.
 */
enum battery_present battery_is_present(void)
{
	enum battery_present batt_pres;

	/* Get the physical hardware status */
	batt_pres = battery_hw_present();

	/*
	 * Make sure battery status is implemented, I2C transactions are
	 * success & the battery status is Initialized to find out if it
	 * is a working battery and it is not in the cut-off mode.
	 *
	 * If battery I2C fails but VBATT is high, battery is booting from
	 * cut-off mode.
	 *
	 * FETs are turned off after Power Shutdown time.
	 * The device will wake up when a voltage is applied to PACK.
	 * Battery status will be inactive until it is initialized.
	 */
	if (batt_pres == BP_YES && batt_pres_prev != batt_pres &&
	    !battery_is_cut_off() && !battery_init()) {
		batt_pres = BP_NO;
	}

	batt_pres_prev = batt_pres;

	return batt_pres;
}


