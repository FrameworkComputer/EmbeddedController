/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* G753 temperature sensor module for Chrome EC */

#include "common.h"
#include "console.h"
#include "g753.h"
#include "gpio.h"
#include "i2c.h"
#include "hooks.h"
#include "util.h"

static int temp_val_local;

/**
 * Determine whether the sensor is powered.
 *
 * @return non-zero the g753 sensor is powered.
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
	return i2c_read8(I2C_PORT_THERMAL, G753_I2C_ADDR_FLAGS,
			 offset, data_ptr);
}

#ifdef CONFIG_CMD_TEMP_SENSOR
static int raw_write8(const int offset, int data)
{
	return i2c_write8(I2C_PORT_THERMAL, G753_I2C_ADDR_FLAGS,
			  offset, data);
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
static int set_temp(const int offset, int temp)
{
	if (temp < -127 || temp > 127)
		return EC_ERROR_INVAL;

	return raw_write8(offset, (uint8_t)temp);
}
#endif

int g753_get_val(int idx, int *temp_ptr)
{
	if (!has_power())
		return EC_ERROR_NOT_POWERED;

	switch (idx) {
	case G753_IDX_INTERNAL:
		*temp_ptr = temp_val_local;
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

static void temp_sensor_poll(void)
{
	if (!has_power())
		return;

	get_temp(G753_TEMP_LOCAL, &temp_val_local);
	temp_val_local = C_TO_K(temp_val_local);
}
DECLARE_HOOK(HOOK_SECOND, temp_sensor_poll, HOOK_PRIO_TEMP_SENSOR);

#ifdef CONFIG_CMD_TEMP_SENSOR
static void print_temps(const char *name,
			const int temp_reg,
			const int high_limit_reg)
{
	int value;

	ccprintf("%s:\n", name);

	if (get_temp(temp_reg, &value) == EC_SUCCESS)
		ccprintf("  Temp:       %3dC\n", value);

	if (get_temp(high_limit_reg, &value) == EC_SUCCESS)
		ccprintf("  High Alarm: %3dC\n", value);

}

static int print_status(void)
{
	int value;

	if (!has_power()) {
		ccprintf("ERROR: Temp sensor not powered.\n");
		return EC_ERROR_NOT_POWERED;
	}

	print_temps("Local", G753_TEMP_LOCAL,
		    G753_LOCAL_TEMP_HIGH_LIMIT_R);

	ccprintf("\n");

	if (raw_read8(G753_STATUS, &value) == EC_SUCCESS)
		ccprintf("STATUS:  %pb\n", BINARY_VALUE(value, 8));

	if (raw_read8(G753_CONFIGURATION_R, &value) == EC_SUCCESS)
		ccprintf("CONFIG:  %pb\n", BINARY_VALUE(value, 8));

	return EC_SUCCESS;
}

static int command_g753(int argc, char **argv)
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
		ccprintf("Byte at offset 0x%02x is %pb\n",
			 offset, BINARY_VALUE(data, 8));
		return rv;
	}

	/* Remaining commands are of the form "g753 set-command offset data" */
	if (argc != 4)
		return EC_ERROR_PARAM_COUNT;

	data = strtoi(argv[3], &e, 0);
	if (*e)
		return EC_ERROR_PARAM3;

	if (!strcasecmp(command, "settemp")) {
		ccprintf("Setting 0x%02x to %dC\n", offset, data);
		rv = set_temp(offset, data);
	} else if (!strcasecmp(command, "setbyte")) {
		ccprintf("Setting 0x%02x to 0x%02x\n", offset, data);
		rv = raw_write8(offset, data);
	} else
		return EC_ERROR_PARAM1;

	return rv;
}
DECLARE_CONSOLE_COMMAND(g753, command_g753,
	"[settemp|setbyte <offset> <value>] or [getbyte <offset>]. "
	"Temps in Celsius.",
	"Print g753 temp sensor status or set parameters.");
#endif
