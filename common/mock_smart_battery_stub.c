/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Smart battery driver.
 */

#include "battery_pack.h"
#include "common.h"
#include "console.h"
#include "smart_battery.h"
#include "smart_battery_stub.h"
#include "uart.h"
#include "util.h"

static uint16_t mock_smart_battery[SB_MANUFACTURER_DATA + 1];

int sb_read(int cmd, int *param)
{
	if (cmd >= ARRAY_SIZE(mock_smart_battery))
		return EC_ERROR_UNIMPLEMENTED;
	if (cmd < 0 || param == NULL)
		return EC_ERROR_INVAL;
	*param = mock_smart_battery[cmd];
	return EC_SUCCESS;
}

int sb_write(int cmd, int param)
{
	if (cmd >= ARRAY_SIZE(mock_smart_battery))
		return EC_ERROR_UNIMPLEMENTED;
	if (cmd < 0)
		return EC_ERROR_INVAL;
	mock_smart_battery[cmd] = param;
	return EC_SUCCESS;
}

int battery_manufacturer_name(char *manufacturer_name, int buf_size)
{
	return EC_SUCCESS;
}

int battery_device_name(char *device_name, int buf_size)
{
	return EC_SUCCESS;
}

int battery_device_chemistry(char *device_chemistry, int buf_size)
{
	return EC_SUCCESS;
}

int battery_current(int *current)
{
	int rv, d;

	rv = sb_read(SB_CURRENT, &d);
	if (rv)
		return rv;

	*current = (int16_t)d;
	return EC_SUCCESS;
}

int battery_average_current(int *current)
{
	int rv, d;

	rv = sb_read(SB_AVERAGE_CURRENT, &d);
	if (rv)
		return rv;

	*current = (int16_t)d;
	return EC_SUCCESS;
}

int battery_time_at_rate(int rate, int *minutes)
{
	return EC_SUCCESS;
}

int battery_manufacturer_date(int *year, int *month, int *day)
{
	return EC_SUCCESS;
}

/* Fake battery */
const struct battery_temperature_ranges bat_temp_ranges = {
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
};

const struct battery_info *battery_get_info(void)
{
	return &bat_info;
}

void battery_vendor_params(struct batt_params *batt)
{
}
