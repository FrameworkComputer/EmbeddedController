/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "adc.h"
#include "battery.h"
#include "battery_smart.h"
#include "util.h"

/* Shutdown mode parameter to write to manufacturer access register */
#define SB_SHUTDOWN_DATA	0x0010

static const struct battery_info info = {
	.voltage_max = 8700,/* mV */
	.voltage_normal = 7600,
	.voltage_min = 6100,
	.precharge_current = 150,/* mA */
	.start_charging_min_c = 0,
	.start_charging_max_c = 45,
	.charging_min_c = 0,
	.charging_max_c = 45,
	.discharging_min_c = -20,
	.discharging_max_c = 60,
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

#ifdef CONFIG_BATTERY_PRESENT_CUSTOM
/*
 * Physical detection of battery via ADC.
 *
 * Upper limit of valid voltage level (mV), when battery is attached to ADC
 * port, is across the internal thermistor with external pullup resistor.
 */
#define BATT_PRESENT_MV  1500
enum battery_present battery_is_present(void)
{
	enum battery_present batt_pres;
	int batt_status;

	/*
	 * if voltage is below certain level (dependent on ratio of
	 * internal thermistor and external pullup resister),
	 * battery is attached.
	 */
	batt_pres = (adc_read_channel(ADC_BATT_PRESENT) > BATT_PRESENT_MV) ?
		BP_NO : BP_YES;

	/*
	 * Make sure battery status is implemented, I2C transactions are
	 * success & the battery status is Initialized to find out if it
	 * is a working battery and it is not in the cut-off mode.
	 *
	 * FETs are turned off after Power Shutdown time.
	 * The device will wake up when a voltage is applied to PACK.
	 * Battery status will be inactive until it is initialized.
	 */
	if (batt_pres == BP_YES && !battery_status(&batt_status))
		if (!(batt_status & STATUS_INITIALIZED))
			batt_pres = BP_NO;

	return batt_pres;
}
#endif
