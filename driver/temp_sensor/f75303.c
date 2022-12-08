/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* F75303 temperature sensor module for Chrome EC */

#include "common.h"
#include "f75303.h"
#include "i2c.h"
#include "hooks.h"
#include "util.h"
#include "console.h"

static int temps[F75303_IDX_COUNT];
static int8_t fake_temp[F75303_IDX_COUNT] = {-1, -1, -1};
static uint8_t f75303_enabled = 1;

/**
 * Enable or disable reading from the sensor
 */
void f75303_set_enabled(uint8_t enabled)
{
	f75303_enabled = enabled;
}

/**
 * Read 8 bits register from temp sensor.
 */
static int raw_read8(const int offset, int *data)
{
	return i2c_read8(I2C_PORT_THERMAL, F75303_I2C_ADDR_FLAGS,
			 offset, data);
}

static int get_temp(const int offset, int *temp)
{
	int rv;
	int temp_raw = 0;

	rv = raw_read8(offset, &temp_raw);
	if (rv != 0) {
		*temp = 0;
		return rv;
	}
	temp_raw = (int8_t)temp_raw;
	*temp = C_TO_K(temp_raw);
	return EC_SUCCESS;
}

int f75303_get_val(int idx, int *temp)
{
	if (idx < 0 || F75303_IDX_COUNT <= idx)
		return EC_ERROR_INVAL;

	if (fake_temp[idx] != -1) {
		*temp = C_TO_K(fake_temp[idx]);
		return EC_SUCCESS;
	}
	if (!f75303_enabled)
		return EC_ERROR_NOT_POWERED;

	*temp = temps[idx];
	return EC_SUCCESS;
}

static void f75303_sensor_poll(void)
{
	if (f75303_enabled) {
		get_temp(F75303_TEMP_LOCAL, &temps[F75303_IDX_LOCAL]);
		get_temp(F75303_TEMP_REMOTE1, &temps[F75303_IDX_REMOTE1]);
		get_temp(F75303_TEMP_REMOTE2, &temps[F75303_IDX_REMOTE2]);
	}
}
DECLARE_HOOK(HOOK_SECOND, f75303_sensor_poll, HOOK_PRIO_TEMP_SENSOR);

static int f75303_set_fake_temp(int argc, char **argv)
{
	int index;
	int value;
	char *e;

	if (argc != 3)
		return EC_ERROR_PARAM_COUNT;

	index = strtoi(argv[1], &e, 0);
	if ((*e) || (index < 0) || (index >= F75303_IDX_COUNT))
		return EC_ERROR_PARAM1;

	if (!strcasecmp(argv[2], "off")) {
		fake_temp[index] = -1;
		ccprintf("Turn off fake temp mode for sensor %u.\n", index);
		return EC_SUCCESS;
	}

	value = strtoi(argv[2], &e, 0);

	if ((*e) || (value < 0) || (value > 100))
		return EC_ERROR_PARAM2;

	fake_temp[index] = value;
	ccprintf("Force sensor %u = %uC.\n", index, value);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(f75303, f75303_set_fake_temp,
		"<index> <value>|off",
		"Set fake temperature of sensor f75303.");
