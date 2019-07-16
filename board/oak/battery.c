/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "battery_smart.h"
#include "i2c.h"
#include "util.h"

/* Shutdown mode parameter to write to manufacturer access register */
#define PARAM_CUT_OFF_LOW  0x10
#define PARAM_CUT_OFF_HIGH 0x00

static const struct battery_info info = {
#if (BOARD_REV >= OAK_REV3)
	.voltage_max = 13050,
	.voltage_normal = 11400,
	/*
	 * TODO(crosbug.com/p/44428):
	 * In order to compatible with 2S battery, set min voltage as 6V rather
         * than 9V. Should set voltage_min to 9V, when 2S battery
	 * phased out.
	 */
	.voltage_min = 6000,
#else /* BOARD_REV < OAK_REV3 */
	.voltage_max = 8700,
	.voltage_normal = 7600,
	.voltage_min = 6000,
#endif
	/* Pre-charge values. */
	.precharge_current = 256, /* mA */

	.start_charging_min_c = 0,
	.start_charging_max_c = 45,
	.charging_min_c = 0,
	.charging_max_c = 45,
	.discharging_min_c = 0,
	.discharging_max_c = 60,
};

const struct battery_info *battery_get_info(void)
{
	return &info;
}

static int cutoff(void)
{
	int rv;
	uint8_t buf[3];

	/* Ship mode command must be sent twice to take effect */
	buf[0] = SB_MANUFACTURER_ACCESS & 0xff;
	buf[1] = PARAM_CUT_OFF_LOW;
	buf[2] = PARAM_CUT_OFF_HIGH;

	i2c_lock(I2C_PORT_BATTERY, 1);
	rv = i2c_xfer_unlocked(I2C_PORT_BATTERY, BATTERY_ADDR_FLAGS,
			       buf, 3, NULL, 0, I2C_XFER_SINGLE);
	rv |= i2c_xfer_unlocked(I2C_PORT_BATTERY, BATTERY_ADDR_FLAGS,
				buf, 3, NULL, 0, I2C_XFER_SINGLE);
	i2c_lock(I2C_PORT_BATTERY, 0);

	return rv;
}

int board_cut_off_battery(void)
{
	return cutoff();
}
