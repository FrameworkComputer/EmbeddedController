/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TMP411 temperature sensor module for Chrome EC */

#include "common.h"
#include "console.h"
#include "tmp411.h"
#include "gpio.h"
#include "i2c.h"
#include "hooks.h"
#include "util.h"

static int temp_val_local;
static int temp_val_remote1;
static uint8_t is_sensor_shutdown;

/**
 * Determine whether the sensor is powered.
 *
 * @return non-zero the tmp411 sensor is powered.
 */
static int has_power(void)
{
#ifdef CONFIG_TEMP_SENSOR_POWER
	return gpio_get_level(GPIO_TEMP_SENSOR_POWER);
#else
	return !is_sensor_shutdown;
#endif
}

static int raw_read8(const int offset, int *data_ptr)
{
	return i2c_read8(I2C_PORT_THERMAL, TMP411_I2C_ADDR, offset, data_ptr);
}

static int raw_write8(const int offset, int data)
{
	return i2c_write8(I2C_PORT_THERMAL, TMP411_I2C_ADDR, offset, data);
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
static int tmp411_set_temp(const int offset, int temp)
{
	if (temp < -127 || temp > 127)
		return EC_ERROR_INVAL;

	return raw_write8(offset, (uint8_t)temp);
}
#endif

int tmp411_get_val(int idx, int *temp_ptr)
{
	if (!has_power())
		return EC_ERROR_NOT_POWERED;

	switch (idx) {
	case TMP411_IDX_LOCAL:
		*temp_ptr = temp_val_local;
		break;
	case TMP411_IDX_REMOTE1:
		*temp_ptr = temp_val_remote1;
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

static int tmp411_shutdown(uint8_t want_shutdown)
{
	int ret, value;

	if (want_shutdown == is_sensor_shutdown)
		return EC_SUCCESS;

	ret = raw_read8(TMP411_CONFIGURATION1_R, &value);
	if (ret < 0) {
		ccprintf("ERROR: Temp sensor I2C read8 error.\n");
		return ret;
	}

	if (want_shutdown && !(value & TMP411_CONFIG1_RUN_L)) {
		/* tmp411 is running, and want it to shutdown */
		/* CONFIG REG1 BIT6: 0=Run, 1=Shutdown */
		/* shut it down */
		value |= TMP411_CONFIG1_RUN_L;
		ret = raw_write8(TMP411_CONFIGURATION1_R, value);
	} else if (!want_shutdown && (value & TMP411_CONFIG1_RUN_L)) {
		/* tmp411 is shutdown, and want turn it on */
		value &= ~TMP411_CONFIG1_RUN_L;
		ret = raw_write8(TMP411_CONFIGURATION1_R, value);
	}
	/* else, the current setting is exactly what you want */

	is_sensor_shutdown = want_shutdown;
	return ret;
}

static int tmp411_set_therm_mode(void)
{
	int ret = 0;
	int data = 0;

	ret = raw_read8(TMP411_CONFIGURATION1_R, &data);
	if (ret)
		return EC_ERROR_UNKNOWN;

	data |= TMP411_CONFIG1_MODE;
	ret = raw_write8(TMP411_CONFIGURATION1_W, data);
	if (ret)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

int tmp411_set_therm_limit(int channel, int limit_c, int hysteresis)
{
	int ret = 0;
	int reg = 0;

	if (channel >= TMP411_CHANNEL_COUNT)
		return EC_ERROR_INVAL;

	if (hysteresis > TMP411_HYSTERESIS_HIGH_LIMIT ||
	    hysteresis < TMP411_HYSTERESIS_LOW_LIMIT)
		return EC_ERROR_INVAL;

	/* hysteresis must be less than high limit */
	if (hysteresis > limit_c)
		return EC_ERROR_INVAL;

	if (tmp411_set_therm_mode() != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	switch (channel) {
	case TMP411_CHANNEL_LOCAL:
		reg = TMP411_LOCAL_HIGH_LIMIT_W;
		break;
	case TMP411_CHANNEL_REMOTE1:
		reg = TMP411_REMOTE1_HIGH_LIMIT_W;
		break;
	}

	ret = raw_write8(reg, limit_c);
	if (ret)
		return EC_ERROR_UNKNOWN;

	ret = raw_write8(TMP411_THERM_HYSTERESIS, hysteresis);
	if (ret)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

static void tmp411_temp_sensor_poll(void)
{
	int temp_c;

	if (!has_power())
		return;

	if (get_temp(TMP411_LOCAL, &temp_c) == EC_SUCCESS)
		temp_val_local = C_TO_K(temp_c);

	if (get_temp(TMP411_REMOTE1, &temp_c) == EC_SUCCESS)
		temp_val_remote1 = C_TO_K(temp_c);
}
DECLARE_HOOK(HOOK_SECOND, tmp411_temp_sensor_poll, HOOK_PRIO_TEMP_SENSOR);

#ifdef CONFIG_CMD_TEMP_SENSOR
static void print_temps(const char *name, const int tmp411_temp_reg,
			const int tmp411_therm_limit_reg,
			const int tmp411_high_limit_reg,
			const int tmp411_low_limit_reg)
{
	int value;

	if (!has_power()) {
		ccprintf("  TMP411 is shutdown\n");
		return;
	}

	ccprintf("%s:\n", name);

	if (get_temp(tmp411_temp_reg, &value) == EC_SUCCESS)
		ccprintf("  Temp       %3dC\n", value);

	if (get_temp(tmp411_therm_limit_reg, &value) == EC_SUCCESS)
		ccprintf("  Therm Trip %3dC\n", value);

	if (get_temp(tmp411_high_limit_reg, &value) == EC_SUCCESS)
		ccprintf("  High Alarm %3dC\n", value);

	if (get_temp(tmp411_low_limit_reg, &value) == EC_SUCCESS)
		ccprintf("  Low Alarm  %3dC\n", value);
}

static int print_status(void)
{
	int value;

	print_temps("Local", TMP411_LOCAL, TMP411_LOCAL_THERM_LIMIT,
		    TMP411_LOCAL_HIGH_LIMIT_R, TMP411_LOCAL_LOW_LIMIT_R);

	print_temps("Remote1", TMP411_REMOTE1, TMP411_REMOTE1_THERM_LIMIT,
		    TMP411_REMOTE1_HIGH_LIMIT_R, TMP411_REMOTE1_LOW_LIMIT_R);

	ccprintf("\n");

	if (raw_read8(TMP411_STATUS_R, &value) == EC_SUCCESS)
		ccprintf("STATUS:  0x%x\n", value);

	if (raw_read8(TMP411_CONFIGURATION1_R, &value) == EC_SUCCESS)
		ccprintf("CONFIG1: 0x%x\n", value);

	return EC_SUCCESS;
}

static int command_tmp411(int argc, const char **argv)
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
			rv = tmp411_set_power(TMP411_POWER_ON);
			if (!rv)
				print_status();
		} else if (!strncasecmp(power, "off", sizeof("off")))
			rv = tmp411_set_power(TMP411_POWER_OFF);
		else
			return EC_ERROR_PARAM2;
		ccprintf("Set TMP411 %s\n", power);
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
		ccprintf("Byte at offset 0x%02x is 0x%x\n", offset, data);
		return rv;
	}

	/* Remaining commands are "tmp411 set-command offset data" */
	if (argc != 4)
		return EC_ERROR_PARAM_COUNT;

	data = strtoi(argv[3], &e, 0);
	if (*e)
		return EC_ERROR_PARAM3;

	if (!strcasecmp(command, "settemp")) {
		ccprintf("Setting 0x%02x to %dC\n", offset, data);
		rv = tmp411_set_temp(offset, data);
	} else if (!strcasecmp(command, "setbyte")) {
		ccprintf("Setting 0x%02x to 0x%02x\n", offset, data);
		rv = raw_write8(offset, data);
	} else
		return EC_ERROR_PARAM1;

	return rv;
}
DECLARE_CONSOLE_COMMAND(
	tmp411, command_tmp411,
	"[settemp|setbyte <offset> <value>] or [getbyte <offset>] or"
	"[power <on|off>]. "
	"Temps in Celsius.",
	"Print tmp411 temp sensor status or set parameters.");
#endif

int tmp411_set_power(enum tmp411_power_state power_on)
{
#ifndef CONFIG_TEMP_SENSOR_POWER
	uint8_t shutdown = (power_on == TMP411_POWER_OFF) ? 1 : 0;

	return tmp411_shutdown(shutdown);
#else
	gpio_set_level(GPIO_TEMP_SENSOR_POWER, power_on);
	return EC_SUCCESS;
#endif
}
