/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "common.h"
#include "i2c.h"

/* Battery temperature ranges in degrees C */
static const struct battery_info info = {
	/* Design voltage */
	.voltage_max    = 4350,
	.voltage_normal = 3800,
	.voltage_min    = 2800,
	/* Pre-charge current: I <= 0.01C */
	.precharge_current  = 64,  /* mA */
	/* Operational temperature range */
	.start_charging_min_c = 0,
	.start_charging_max_c = 45,
	.charging_min_c       = 0,
	.charging_max_c       = 50,
	.discharging_min_c    = -20,
	.discharging_max_c    = 60,
};

const struct battery_info *battery_get_info(void)
{
	return &info;
}

int board_cut_off_battery(void)
{
	/* Write SET_SHUTDOWN(0x13) to CTRL(0x00) */
	return i2c_write16(I2C_PORT_BATTERY, 0xaa, 0x0, 0x13);
}
