/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* F75303 temperature sensor module for Chrome EC */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "i2c.h"
#include "math_util.h"
#include "temp_sensor/f75303.h"
#include "util.h"

#ifdef CONFIG_ZEPHYR
#include "temp_sensor/temp_sensor.h"
#endif

#define F75303_RESOLUTION 11
#define F75303_SHIFT1 (16 - F75303_RESOLUTION)
#define F75303_SHIFT2 (F75303_RESOLUTION - 8)

static int temps[F75303_IDX_COUNT];
static int8_t fake_temp[F75303_IDX_COUNT];

#ifndef CONFIG_ZEPHYR
/**
 * Read 8 bits register from temp sensor.
 */
static int raw_read8(const int offset, int *data)
{
	return i2c_read8(I2C_PORT_THERMAL, F75303_I2C_ADDR_FLAGS, offset, data);
}
#else
/**
 * Read 8 bits register from temp sensor.
 */
static int raw_read8(int sensor, const int offset, int *data)
{
	return i2c_read8(f75303_sensors[sensor].i2c_port,
			 f75303_sensors[sensor].i2c_addr_flags, offset, data);
}
#endif /* !CONFIG_ZEPHYR */

#ifndef CONFIG_ZEPHYR
static int get_temp(const int offset, int *temp)
{
	int rv;
	int temp_raw = 0;

	rv = raw_read8(offset, &temp_raw);
	if (rv != 0)
		return rv;

	*temp = C_TO_K(temp_raw);
	return EC_SUCCESS;
}
#else
static int get_temp(int sensor, const int offset, int *temp)
{
	int rv;
	int temp_raw = 0;

	rv = raw_read8(sensor, offset, &temp_raw);
	if (rv != 0)
		return rv;

	*temp = CELSIUS_TO_MILLI_KELVIN(temp_raw);
	return EC_SUCCESS;
}
#endif /* !CONFIG_ZEPHYR */

int f75303_get_val(int idx, int *temp)
{
	if (idx < 0 || F75303_IDX_COUNT <= idx)
		return EC_ERROR_INVAL;

	if (fake_temp[idx] != -1) {
		*temp = C_TO_K(fake_temp[idx]);
		return EC_SUCCESS;
	}

	*temp = temps[idx];
	return EC_SUCCESS;
}

static inline int f75303_reg_to_mk(int16_t reg)
{
	int temp_mc;

	temp_mc = (((reg >> F75303_SHIFT1) * 1000) >> F75303_SHIFT2);

	return MILLI_CELSIUS_TO_MILLI_KELVIN(temp_mc);
}

int f75303_get_val_k(int idx, int *temp_k_ptr)
{
	if (idx >= F75303_IDX_COUNT)
		return EC_ERROR_INVAL;

	*temp_k_ptr = MILLI_KELVIN_TO_KELVIN(temps[idx]);
	return EC_SUCCESS;
}

int f75303_get_val_mk(int idx, int *temp_mk_ptr)
{
	if (idx >= F75303_IDX_COUNT)
		return EC_ERROR_INVAL;

	*temp_mk_ptr = temps[idx];
	return EC_SUCCESS;
}

#ifndef CONFIG_ZEPHYR
static void f75303_sensor_poll(void)
{
	get_temp(F75303_TEMP_LOCAL_REGISTER, &temps[F75303_IDX_LOCAL]);
	get_temp(F75303_TEMP_REMOTE1_REGISTER, &temps[F75303_IDX_REMOTE1]);
	get_temp(F75303_TEMP_REMOTE2_REGISTER, &temps[F75303_IDX_REMOTE2]);
}
DECLARE_HOOK(HOOK_SECOND, f75303_sensor_poll, HOOK_PRIO_TEMP_SENSOR);
#else
void f75303_update_temperature(int idx)
{
	int temp_reg = 0;
	int rv;

	if (idx >= F75303_IDX_COUNT)
		return;

	switch (idx) {
	case F75303_IDX_LOCAL:
		rv = get_temp(idx, F75303_TEMP_LOCAL_REGISTER, &temp_reg);
		break;
	case F75303_IDX_REMOTE1:
		rv = get_temp(idx, F75303_TEMP_REMOTE1_REGISTER, &temp_reg);
		break;
	case F75303_IDX_REMOTE2:
		rv = get_temp(idx, F75303_TEMP_REMOTE2_REGISTER, &temp_reg);
		break;
	default:
		rv = EC_ERROR_INVAL;
	}

	if (rv == EC_SUCCESS) {
		temps[idx] = temp_reg;
	}
}
#endif /* CONFIG_ZEPHYR */

static int f75303_set_fake_temp(int argc, const char **argv)
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
DECLARE_CONSOLE_COMMAND(f75303, f75303_set_fake_temp, "<index> <value>|off",
			"Set fake temperature of sensor f75303.");

static void f75303_init(void)
{
	int index;

	for (index = 0; index < F75303_IDX_COUNT; index++)
		fake_temp[index] = -1;
}
DECLARE_HOOK(HOOK_INIT, f75303_init, HOOK_PRIO_TEMP_SENSOR);
