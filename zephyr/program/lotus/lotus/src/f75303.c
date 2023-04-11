/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* F75303 temperature sensor module for Chrome EC -Framework only */

#include "common.h"
#include "i2c.h"
#include "hooks.h"
#include "temp_sensor/f75303.h"
#include "temp_sensor/temp_sensor.h"
#include "util.h"
#include "lotus/gpu.h"
#include "console.h"

static int temps[F75303_COUNT];
static int8_t fake_temp[F75303_COUNT] = { -1, -1, -1, -1, -1, -1};

/**
 * Read 8 bits register from temp sensor.
 */
static int raw_read8(int sensor, const int offset, int *data)
{
	return i2c_read8(f75303_sensors[sensor].i2c_port,
			 f75303_sensors[sensor].i2c_addr_flags, offset, data);
}

static int get_temp(int sensor, const int offset, int *temp)
{
	int rv;
	int temp_raw = 0;

	rv = raw_read8(sensor, offset, &temp_raw);
	if (rv != 0)
		return rv;

	*temp = C_TO_K(temp_raw);
	return EC_SUCCESS;
}

int f75303_get_val(int idx, int *temp)
{
	if (idx < 0 || F75303_COUNT <= idx)
		return EC_ERROR_INVAL;

	if (fake_temp[idx] != -1) {
		*temp = C_TO_K(fake_temp[idx]);
		return EC_SUCCESS;
	}

	if (!gpu_present() && (idx < 3))
		*temp = 0;
	else
		*temp = temps[idx];
	return EC_SUCCESS;
}

void f75303_update_temperature(int idx)
{
	int temp_reg = 0;
	int rv = 0;

	if (idx >= F75303_COUNT)
		return;
	switch (idx) {
	/* gpu_vr_f75303 */
	case 0:
		if (gpu_present())
			rv = get_temp(idx, F75303_TEMP_LOCAL, &temp_reg);
		else
			rv = EC_ERROR_NOT_POWERED;
		break;
	/* gpu_vram_f75303 */
	case 1:
		if (gpu_present())
			rv = get_temp(idx, F75303_TEMP_REMOTE1, &temp_reg);
		else
			rv = EC_ERROR_NOT_POWERED;
		break;
	/* gpu_amb_f75303 */
	case 2:
		if (gpu_present())
			rv = get_temp(idx, F75303_TEMP_REMOTE2, &temp_reg);
		else
			rv = EC_ERROR_NOT_POWERED;
		break;
	/* local_f75303 */
	case 3:
		rv = get_temp(idx, F75303_TEMP_LOCAL, &temp_reg);
		break;
	/* ddr_f75303 */
	case 4:
		rv = get_temp(idx, F75303_TEMP_REMOTE1, &temp_reg);
		break;
	/* cpu_f75303 */
	case 5:
		rv = get_temp(idx, F75303_TEMP_REMOTE2, &temp_reg);
		break;
	}
	if (rv == EC_SUCCESS)
		temps[idx] = temp_reg;
}

static int f75303_set_fake_temp(int argc, const char **argv)
{
	int index;
	int value;
	char *e;

	if (argc != 3)
		return EC_ERROR_PARAM_COUNT;

	index = strtoi(argv[1], &e, 0);
	if ((*e) || (index < 0) || (index >= F75303_COUNT))
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
DECLARE_CONSOLE_COMMAND(f75303, f75303_set_fake_temp, "<index> <value>|off",
			"Set fake temperature of sensor f75303.");
