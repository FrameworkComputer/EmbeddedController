/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TMP468 temperature sensor module for Chrome EC */

#include "common.h"
#include "console.h"
#include "tmp432.h"
#include "gpio.h"
#include "i2c.h"
#include "hooks.h"
#include "util.h"

#include "tmp468.h"


static int fake_temp[TMP468_CHANNEL_COUNT] = {-1, -1, -1, -1, -1, -1, -1 , -1, -1};
static int temp_val[TMP468_CHANNEL_COUNT]  = {0, 0, 0, 0, 0, 0, 0 , 0, 0};
static uint8_t is_sensor_shutdown;

static int has_power(void)
{
	return !is_sensor_shutdown;
}

static int raw_read16(const int offset, int *data_ptr)
{
	return i2c_read16(I2C_PORT_THERMAL, TMP468_I2C_ADDR_FLAGS,
			  offset, data_ptr);
}

static int raw_write16(const int offset, int data_ptr)
{
	return i2c_write16(I2C_PORT_THERMAL, TMP468_I2C_ADDR_FLAGS,
			   offset, data_ptr);
}

static int tmp468_shutdown(uint8_t want_shutdown)
{
	int ret, value;

	if (want_shutdown == is_sensor_shutdown)
		return EC_SUCCESS;

	ret = raw_read16(TMP468_CONFIGURATION, &value);
	if (ret < 0) {
		ccprintf("ERROR: Temp sensor I2C read16 error.\n");
		return ret;
	}

	if (want_shutdown)
		value |= TMP468_SHUTDOWN;
	else
		value &= ~TMP468_SHUTDOWN;

	ret = raw_write16(TMP468_CONFIGURATION, value);
	if (ret == EC_SUCCESS)
		is_sensor_shutdown = want_shutdown;

	return EC_SUCCESS;
}

int tmp468_get_val(int idx, int *temp_ptr)
{
	if(!has_power())
		return EC_ERROR_NOT_POWERED;

	if (idx < TMP468_CHANNEL_COUNT) {
		*temp_ptr = C_TO_K(temp_val[idx]);
		return EC_SUCCESS;
	}

	return EC_ERROR_INVAL;
}

static void temp_sensor_poll(void)
{
	int i, ret;

	if (!has_power())
		return;

	for (i = 0; i < TMP468_CHANNEL_COUNT; i++)
		if (fake_temp[i] != -1) {
			temp_val[i] = fake_temp[i];
		} else {
			ret = raw_read16(TMP468_LOCAL + i, &temp_val[i]);
			if (ret < 0)
				return;
			temp_val[i] >>= TMP468_SHIFT1;
		}
}
DECLARE_HOOK(HOOK_SECOND, temp_sensor_poll, HOOK_PRIO_TEMP_SENSOR);

int tmp468_set_power(enum tmp468_power_state power_on)
{
	uint8_t shutdown = (power_on == TMP468_POWER_OFF) ? 1 : 0;
	return tmp468_shutdown(shutdown);
}
