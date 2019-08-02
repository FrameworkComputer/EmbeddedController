/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ADT7481 temperature sensor module for Chrome EC */

#include "common.h"
#include "console.h"
#include "adt7481.h"
#include "gpio.h"
#include "i2c.h"
#include "hooks.h"
#include "util.h"

static int temp_val_local;
static int temp_val_remote1;
static int temp_val_remote2;
static uint8_t is_sensor_shutdown;

/**
 * Determine whether the sensor is powered.
 *
 * @return non-zero the adt7481 sensor is powered.
 */
static int has_power(void)
{
#ifdef CONFIG_TEMP_SENSOR_POWER_GPIO
	return gpio_get_level(CONFIG_TEMP_SENSOR_POWER_GPIO);
#else
	return !is_sensor_shutdown;
#endif
}

static int raw_read8(const int offset, int *data_ptr)
{
	return i2c_read8(I2C_PORT_THERMAL, ADT7481_I2C_ADDR_FLAGS,
			 offset, data_ptr);
}

static int raw_write8(const int offset, int data)
{
	return i2c_write8(I2C_PORT_THERMAL, ADT7481_I2C_ADDR_FLAGS,
			  offset, data);
}

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
static int adt7481_set_temp(const int offset, int temp)
{
	if (temp < -127 || temp > 127)
		return EC_ERROR_INVAL;

	return raw_write8(offset, (uint8_t)temp);
}
#endif

int adt7481_get_val(int idx, int *temp_ptr)
{
	if (!has_power())
		return EC_ERROR_NOT_POWERED;

	switch (idx) {
	case ADT7481_IDX_LOCAL:
		*temp_ptr = temp_val_local;
		break;
	case ADT7481_IDX_REMOTE1:
		*temp_ptr = temp_val_remote1;
		break;
	case ADT7481_IDX_REMOTE2:
		*temp_ptr = temp_val_remote2;
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

static int adt7481_shutdown(uint8_t want_shutdown)
{
	int ret, value;

	if (want_shutdown == is_sensor_shutdown)
		return EC_SUCCESS;

	ret = raw_read8(ADT7481_CONFIGURATION1_R, &value);
	if (ret < 0) {
		ccprintf("ERROR: Temp sensor I2C read8 error.\n");
		return ret;
	}

	if (want_shutdown && !(value & ADT7481_CONFIG1_RUN_L)) {
		/* adt7481 is running, and want it to shutdown */
		/* CONFIG REG1 BIT6: 0=Run, 1=Shutdown */
		/* shut it down */
		value |= ADT7481_CONFIG1_RUN_L;
		ret = raw_write8(ADT7481_CONFIGURATION1_R, value);
	} else if (!want_shutdown && (value & ADT7481_CONFIG1_RUN_L)) {
		/* adt7481 is shutdown, and want turn it on */
		value &= ~ADT7481_CONFIG1_RUN_L;
		ret = raw_write8(ADT7481_CONFIGURATION1_R, value);
	}
	/* else, the current setting is exactly what you want */

	is_sensor_shutdown = want_shutdown;
	return ret;
}

static int adt7481_set_therm_mode(void)
{
	int ret = 0;
	int data = 0;

	ret = raw_read8(ADT7481_CONFIGURATION1_R, &data);
	if (ret)
		return EC_ERROR_UNKNOWN;

	data |= ADT7481_CONFIG1_MODE;
	ret = raw_write8(ADT7481_CONFIGURATION1_W, data);
	if (ret)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

int adt7481_set_therm_limit(int channel, int limit_c, int hysteresis)
{
	int ret = 0;
	int reg = 0;

	if (channel >= ADT7481_CHANNEL_COUNT)
		return EC_ERROR_INVAL;

	if (hysteresis > ADT7481_HYSTERESIS_HIGH_LIMIT ||
		hysteresis < ADT7481_HYSTERESIS_LOW_LIMIT)
		return EC_ERROR_INVAL;

	/* hysteresis must be less than high limit */
	if (hysteresis > limit_c)
		return EC_ERROR_INVAL;

	if (adt7481_set_therm_mode() != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	switch (channel) {
	case ADT7481_CHANNEL_LOCAL:
		reg = ADT7481_LOCAL_HIGH_LIMIT_W;
		break;
	case ADT7481_CHANNEL_REMOTE1:
		reg = ADT7481_REMOTE1_HIGH_LIMIT_W;
		break;
	case ADT7481_CHANNEL_REMOTE2:
		reg = ADT7481_REMOTE2_HIGH_LIMIT;
		break;
	}

	ret = raw_write8(reg, limit_c);
	if (ret)
		return EC_ERROR_UNKNOWN;

	ret = raw_write8(ADT7481_THERM_HYSTERESIS, hysteresis);
	if (ret)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

static void adt7481_temp_sensor_poll(void)
{
	int temp_c;

	if (!has_power())
		return;

	if (get_temp(ADT7481_LOCAL, &temp_c) == EC_SUCCESS)
		temp_val_local = C_TO_K(temp_c);

	if (get_temp(ADT7481_REMOTE1, &temp_c) == EC_SUCCESS)
		temp_val_remote1 = C_TO_K(temp_c);

	if (get_temp(ADT7481_REMOTE2, &temp_c) == EC_SUCCESS)
		temp_val_remote2 = C_TO_K(temp_c);
}
DECLARE_HOOK(HOOK_SECOND, adt7481_temp_sensor_poll, HOOK_PRIO_TEMP_SENSOR);

#ifdef CONFIG_CMD_TEMP_SENSOR
static void print_temps(
		const char *name,
		const int adt7481_temp_reg,
		const int adt7481_therm_limit_reg,
		const int adt7481_high_limit_reg,
		const int adt7481_low_limit_reg)
{
	int value;

	if (!has_power()) {
		ccprintf("  ADT7481 is shutdown\n");
		return;
	}

	ccprintf("%s:\n", name);

	if (get_temp(adt7481_temp_reg, &value) == EC_SUCCESS)
		ccprintf("  Temp       %3dC\n", value);

	if (get_temp(adt7481_therm_limit_reg, &value) == EC_SUCCESS)
		ccprintf("  Therm Trip %3dC\n", value);

	if (get_temp(adt7481_high_limit_reg, &value) == EC_SUCCESS)
		ccprintf("  High Alarm %3dC\n", value);

	if (get_temp(adt7481_low_limit_reg, &value) == EC_SUCCESS)
		ccprintf("  Low Alarm  %3dC\n", value);
}

static int print_status(void)
{
	int value;

	print_temps("Local", ADT7481_LOCAL,
		    ADT7481_LOCAL_THERM_LIMIT,
		    ADT7481_LOCAL_HIGH_LIMIT_R,
		    ADT7481_LOCAL_LOW_LIMIT_R);

	print_temps("Remote1", ADT7481_REMOTE1,
		    ADT7481_REMOTE1_THERM_LIMIT,
		    ADT7481_REMOTE1_HIGH_LIMIT_R,
		    ADT7481_REMOTE1_LOW_LIMIT_R);

	print_temps("Remote2", ADT7481_REMOTE2,
		    ADT7481_REMOTE2_THERM_LIMIT,
		    ADT7481_REMOTE2_HIGH_LIMIT,
		    ADT7481_REMOTE2_LOW_LIMIT);

	ccprintf("\n");

	if (raw_read8(ADT7481_STATUS1_R, &value) == EC_SUCCESS)
		ccprintf("STATUS1:  %pb\n", BINARY_VALUE(value, 8));

	if (raw_read8(ADT7481_STATUS2_R, &value) == EC_SUCCESS)
		ccprintf("STATUS2:  %pb\n", BINARY_VALUE(value, 8));

	if (raw_read8(ADT7481_CONFIGURATION1_R, &value) == EC_SUCCESS)
		ccprintf("CONFIG1: %pb\n", BINARY_VALUE(value, 8));

	if (raw_read8(ADT7481_CONFIGURATION2, &value) == EC_SUCCESS)
		ccprintf("CONFIG2: %pb\n", BINARY_VALUE(value, 8));

	return EC_SUCCESS;
}

static int command_adt7481(int argc, char **argv)
{
	char *command;
	char *e;
	char *power;
	int data;
	int offset;
	int rv;

	/* handle "power" command before checking the power status. */
	if ((argc == 3) && !strcasecmp(argv[1], "power")) {
		power = argv[2];
		if (!strncasecmp(power, "on", sizeof("on"))) {
			rv = adt7481_set_power(ADT7481_POWER_ON);
			if (!rv)
				print_status();
		} else if (!strncasecmp(power, "off", sizeof("off")))
			rv = adt7481_set_power(ADT7481_POWER_OFF);
		else
			return EC_ERROR_PARAM2;
		ccprintf("Set ADT7481 %s\n", power);
		return rv;
	}

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

	/* Remaining commands are "adt7481 set-command offset data" */
	if (argc != 4)
		return EC_ERROR_PARAM_COUNT;

	data = strtoi(argv[3], &e, 0);
	if (*e)
		return EC_ERROR_PARAM3;

	if (!strcasecmp(command, "settemp")) {
		ccprintf("Setting 0x%02x to %dC\n", offset, data);
		rv = adt7481_set_temp(offset, data);
	} else if (!strcasecmp(command, "setbyte")) {
		ccprintf("Setting 0x%02x to 0x%02x\n", offset, data);
		rv = raw_write8(offset, data);
	} else
		return EC_ERROR_PARAM1;

	return rv;
}
DECLARE_CONSOLE_COMMAND(adt7481, command_adt7481,
	"[settemp|setbyte <offset> <value>] or [getbyte <offset>] or"
	"[power <on|off>]. "
	"Temps in Celsius.",
	"Print tmp432 temp sensor status or set parameters.");
#endif

int adt7481_set_power(enum adt7481_power_state power_on)
{
#ifndef CONFIG_TEMP_SENSOR_POWER_GPIO
	uint8_t shutdown = (power_on == ADT7481_POWER_OFF) ? 1 : 0;

	return adt7481_shutdown(shutdown);
#else
	gpio_set_level(CONFIG_TEMP_SENSOR_POWER_GPIO, power_on);
	return EC_SUCCESS;
#endif
}

