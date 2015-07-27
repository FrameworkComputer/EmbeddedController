/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TMP432 temperature sensor module for Chrome EC */

#include "common.h"
#include "console.h"
#include "tmp432.h"
#include "gpio.h"
#include "i2c.h"
#include "hooks.h"
#include "util.h"

static int temp_val_local;
static int temp_val_remote1;
static int temp_val_remote2;

/**
 * Determine whether the sensor is powered.
 *
 * @return non-zero the tmp432 sensor is powered.
 */
static int has_power(void)
{
#ifdef CONFIG_TEMP_SENSOR_POWER_GPIO
	return gpio_get_level(CONFIG_TEMP_SENSOR_POWER_GPIO);
#else
	return 1;
#endif
}

static int raw_read8(const int offset, int *data_ptr)
{
	return i2c_read8(I2C_PORT_THERMAL, TMP432_I2C_ADDR, offset, data_ptr);
}

#ifdef CONFIG_CMD_TEMP_SENSOR
static int raw_write8(const int offset, int data)
{
	return i2c_write8(I2C_PORT_THERMAL, TMP432_I2C_ADDR, offset, data);
}
#endif

static int get_temp(const int offset, int *temp_ptr)
{
	int rv;
	int temp_raw = 0;

	rv = raw_read8(offset, &temp_raw);
	if (rv < 0)
		return rv;

	*temp_ptr = (int)(int8_t)temp_raw;
	return EC_SUCCESS;
}

#ifdef CONFIG_CMD_TEMP_SENSOR
static int tmp432_set_temp(const int offset, int temp)
{
	if (temp < -127 || temp > 127)
		return EC_ERROR_INVAL;

	return raw_write8(offset, (uint8_t)temp);
}
#endif

int tmp432_get_val(int idx, int *temp_ptr)
{
	if (!has_power())
		return EC_ERROR_NOT_POWERED;

	switch (idx) {
	case TMP432_IDX_LOCAL:
		*temp_ptr = temp_val_local;
		break;
	case TMP432_IDX_REMOTE1:
		*temp_ptr = temp_val_remote1;
		break;
	case TMP432_IDX_REMOTE2:
		*temp_ptr = temp_val_remote2;
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

static void temp_sensor_poll(void)
{
	int temp_c;

	if (!has_power())
		return;

	if (get_temp(TMP432_LOCAL, &temp_c) == EC_SUCCESS)
		temp_val_local = C_TO_K(temp_c);

	if (get_temp(TMP432_REMOTE1, &temp_c) == EC_SUCCESS)
		temp_val_remote1 = C_TO_K(temp_c);

	if (get_temp(TMP432_REMOTE2, &temp_c) == EC_SUCCESS)
		temp_val_remote2 = C_TO_K(temp_c);
}
DECLARE_HOOK(HOOK_SECOND, temp_sensor_poll, HOOK_PRIO_TEMP_SENSOR);

#ifdef CONFIG_CMD_TEMP_SENSOR
static void print_temps(
		const char *name,
		const int tmp432_temp_reg,
		const int tmp432_therm_limit_reg,
		const int tmp432_high_limit_reg,
		const int tmp432_low_limit_reg)
{
	int value;

	ccprintf("%s:\n", name);

	if (get_temp(tmp432_temp_reg, &value) == EC_SUCCESS)
		ccprintf("  Temp       %3dC\n", value);

	if (get_temp(tmp432_therm_limit_reg, &value) == EC_SUCCESS)
		ccprintf("  Therm Trip %3dC\n", value);

	if (get_temp(tmp432_high_limit_reg, &value) == EC_SUCCESS)
		ccprintf("  High Alarm %3dC\n", value);

	if (get_temp(tmp432_low_limit_reg, &value) == EC_SUCCESS)
		ccprintf("  Low Alarm  %3dC\n", value);
}

static int print_status(void)
{
	int value;

	print_temps("Local", TMP432_LOCAL,
		    TMP432_LOCAL_THERM_LIMIT,
		    TMP432_LOCAL_HIGH_LIMIT_R,
		    TMP432_LOCAL_LOW_LIMIT_R);

	print_temps("Remote1", TMP432_REMOTE1,
		    TMP432_REMOTE1_THERM_LIMIT,
		    TMP432_REMOTE1_HIGH_LIMIT_R,
		    TMP432_REMOTE1_LOW_LIMIT_R);

	print_temps("Remote2", TMP432_REMOTE2,
		    TMP432_REMOTE2_THERM_LIMIT,
		    TMP432_REMOTE2_HIGH_LIMIT_R,
		    TMP432_REMOTE2_LOW_LIMIT_R);

	ccprintf("\n");

	if (raw_read8(TMP432_STATUS, &value) == EC_SUCCESS)
		ccprintf("STATUS:  %08b\n", value);

	if (raw_read8(TMP432_CONFIGURATION1_R, &value) == EC_SUCCESS)
		ccprintf("CONFIG1: %08b\n", value);

	if (raw_read8(TMP432_CONFIGURATION2_R, &value) == EC_SUCCESS)
		ccprintf("CONFIG2: %08b\n", value);

	return EC_SUCCESS;
}

static int command_tmp432(int argc, char **argv)
{
	char *command;
	char *e;
	int data;
	int offset;
	int rv;

	if (!has_power()) {
		ccprintf("ERROR: Temp sensor not powered.\n");
		return EC_ERROR_NOT_POWERED;
	}

	/* If no args just print status */
	if (argc == 1)
		return print_status();

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	command = argv[1];
	offset = strtoi(argv[2], &e, 0);
	if (*e || offset < 0 || offset > 255)
		return EC_ERROR_PARAM2;

	if (!strcasecmp(command, "getbyte")) {
		rv = raw_read8(offset, &data);
		if (rv < 0)
			return rv;
		ccprintf("Byte at offset 0x%02x is %08b\n", offset, data);
		return rv;
	}

	/* Remaining commands are "tmp432 set-command offset data" */
	if (argc != 4)
		return EC_ERROR_PARAM_COUNT;

	data = strtoi(argv[3], &e, 0);
	if (*e)
		return EC_ERROR_PARAM3;

	if (!strcasecmp(command, "settemp")) {
		ccprintf("Setting 0x%02x to %dC\n", offset, data);
		rv = tmp432_set_temp(offset, data);
	} else if (!strcasecmp(command, "setbyte")) {
		ccprintf("Setting 0x%02x to 0x%02x\n", offset, data);
		rv = raw_write8(offset, data);
	} else
		return EC_ERROR_PARAM1;

	return rv;
}
DECLARE_CONSOLE_COMMAND(tmp432, command_tmp432,
	"[settemp|setbyte <offset> <value>] or [getbyte <offset>]. "
	"Temps in Celsius.",
	"Print tmp432 temp sensor status or set parameters.", NULL);
#endif
