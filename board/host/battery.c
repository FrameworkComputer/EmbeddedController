/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Smart battery driver.
 */

#include "battery.h"
#include "battery_smart.h"
#include "common.h"
#include "console.h"
#include "test_util.h"
#include "util.h"

static uint16_t mock_smart_battery[SB_MANUFACTURER_DATA + 1];

int sb_i2c_xfer(int port, int slave_addr, const uint8_t *out, int out_size,
		uint8_t *in, int in_size, int flags)
{
	if (out_size == 0)
		return EC_SUCCESS;

	if (port != I2C_PORT_BATTERY || slave_addr != BATTERY_ADDR)
		return EC_ERROR_INVAL;
	if (out[0]  >= ARRAY_SIZE(mock_smart_battery))
		return EC_ERROR_UNIMPLEMENTED;
	if (out_size == 1) {
		/* Read */
		if (in_size != 2)
			/* We are not doing a read16, assume read string */
			return EC_SUCCESS;
		else
			*(uint16_t *)in = mock_smart_battery[out[0]];
	} else {
		/* write */
		if (out_size != 3)
			/* We are only expecting write 16 */
			return EC_ERROR_UNIMPLEMENTED;
		else
			mock_smart_battery[out[0]] = (out[2] << 8) | out[1];
	}
	return EC_SUCCESS;
}
DECLARE_TEST_I2C_XFER(sb_i2c_xfer);

int battery_time_at_rate(int rate, int *minutes)
{
	return EC_SUCCESS;
}

static const struct battery_info bat_info = {
	/*
	 * Design voltage
	 *   max    = 8.4V
	 *   normal = 7.4V
	 *   min    = 6.0V
	 */
	.voltage_max    = 8400,
	.voltage_normal = 7400,
	.voltage_min    = 6000,

	/* Pre-charge current: I <= 0.01C */
	.precharge_current  = 64, /* mA */

	/*
	 * Operational temperature range
	 *   0 <= T_charge    <= 50 deg C
	 * -20 <= T_discharge <= 60 deg C
	 */
	.start_charging_min_c = 0,
	.start_charging_max_c = 50,
	.charging_min_c       = 0,
	.charging_max_c       = 50,
	.discharging_min_c    = -20,
	.discharging_max_c    = 60,
};

const struct battery_info *battery_get_info(void)
{
	return &bat_info;
}
