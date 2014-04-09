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

int sb_i2c_read16(int port, int slave_addr, int offset, int *data)
{
	if (port != I2C_PORT_BATTERY || slave_addr != BATTERY_ADDR)
		return EC_ERROR_INVAL;
	if (offset >= ARRAY_SIZE(mock_smart_battery))
		return EC_ERROR_UNIMPLEMENTED;
	if (offset < 0 || data == NULL)
		return EC_ERROR_INVAL;
	*data = mock_smart_battery[offset];
	return EC_SUCCESS;
}
DECLARE_TEST_I2C_READ16(sb_i2c_read16);

int sb_i2c_write16(int port, int slave_addr, int offset, int data)
{
	if (port != I2C_PORT_BATTERY || slave_addr != BATTERY_ADDR)
		return EC_ERROR_INVAL;
	if (offset >= ARRAY_SIZE(mock_smart_battery))
		return EC_ERROR_UNIMPLEMENTED;
	if (offset < 0)
		return EC_ERROR_INVAL;
	mock_smart_battery[offset] = data;
	return EC_SUCCESS;
}
DECLARE_TEST_I2C_WRITE16(sb_i2c_write16);

int sb_i2c_read_string(int port, int slave_addr, int offset, uint8_t *data,
		       int len)
{
	return EC_SUCCESS;
}
DECLARE_TEST_I2C_READ_STRING(sb_i2c_read_string);

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
