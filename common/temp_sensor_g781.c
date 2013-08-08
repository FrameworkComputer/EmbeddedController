/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* G781 temperature sensor module for Chrome EC */

#include "common.h"
#include "console.h"
#include "i2c.h"
#include "temp_sensor_g781.h"
#include "util.h"

static int g781_read8(const int offset, int *data_ptr)
{
	return i2c_read8(I2C_PORT_THERMAL, G781_I2C_ADDR, offset, data_ptr);
}

static int g781_write8(const int offset, int data)
{
	return i2c_write8(I2C_PORT_THERMAL, G781_I2C_ADDR, offset, data);
}

static int g781_get_temp(const int offset, int *temp_ptr)
{
	int rv;
	int temp_raw = 0;

	rv = g781_read8(offset, &temp_raw);
	if (rv < 0)
		return rv;

	*temp_ptr = (int)(int8_t)temp_raw;
	return EC_SUCCESS;
}

static int g781_set_temp(const int offset, int temp)
{
	if (temp < -127 || temp > 127)
		return EC_ERROR_INVAL;

	return g781_write8(offset, (uint8_t)temp);
}

int g781_get_val(int idx, int *temp_ptr)
{
	int offset;
	int rv;

	if (!board_g781_has_power())
		return EC_ERROR_NOT_POWERED;

	switch (idx) {
	case 0:
		offset = G781_TEMP_LOCAL;
		break;
	case 1:
		offset = G781_TEMP_REMOTE;
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}

	rv = g781_get_temp(offset, temp_ptr);
	if (rv < 0)
		return rv;

	/* Temperature from sensor is in degrees Celsius */
	*temp_ptr = C_TO_K(*temp_ptr);
	return EC_SUCCESS;
}

static int g781_show_status(void)
{
	int value;
	int rv;


	rv = g781_get_temp(G781_TEMP_LOCAL, &value);
	if (rv < 0)
		return rv;
	ccprintf("Local Temp:   %3dC\n", value);

	rv = g781_get_temp(G781_LOCAL_TEMP_THERM_LIMIT, &value);
	if (rv < 0)
		return rv;
	ccprintf("  Therm Trip: %3dC\n", value);

	rv = g781_get_temp(G781_LOCAL_TEMP_HIGH_LIMIT_R, &value);
	if (rv < 0)
		return rv;
	ccprintf("  High Alarm: %3dC\n", value);

	rv = g781_get_temp(G781_LOCAL_TEMP_LOW_LIMIT_R, &value);
	if (rv < 0)
		return rv;
	ccprintf("  Low Alarm:  %3dC\n", value);

	rv = g781_get_temp(G781_TEMP_REMOTE, &value);
	if (rv < 0)
		return rv;
	ccprintf("Remote Temp:  %3dC\n", value);

	rv = g781_get_temp(G781_REMOTE_TEMP_THERM_LIMIT, &value);
	if (rv < 0)
		return rv;
	ccprintf("  Therm Trip: %3dC\n", value);

	rv = g781_get_temp(G781_REMOTE_TEMP_HIGH_LIMIT_R, &value);
	if (rv < 0)
		return rv;
	ccprintf("  High Alarm: %3dC\n", value);

	rv = g781_get_temp(G781_REMOTE_TEMP_LOW_LIMIT_R, &value);
	if (rv < 0)
		return rv;
	ccprintf("  Low Alarm:  %3dC\n", value);

	rv = g781_read8(G781_STATUS, &value);
	if (rv < 0)
		return rv;
	ccprintf("\nSTATUS: %08b\n", value);

	rv = g781_read8(G781_CONFIGURATION_R, &value);
	if (rv < 0)
		return rv;
	ccprintf("CONFIG: %08b\n", value);

	return EC_SUCCESS;
}

static int command_g781(int argc, char **argv)
{
	char *command;
	char *e;
	int data;
	int offset;
	int rv;

	if (!board_g781_has_power()) {
		ccprintf("ERROR: Temp sensor not powered.\n");
		return EC_ERROR_NOT_POWERED;
	}

	/* If no args just print status */
	if (argc == 1)
		return g781_show_status();

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	command = argv[1];
	offset = strtoi(argv[2], &e, 0);
	if (*e || offset < 0 || offset > 255)
		return EC_ERROR_PARAM2;

	if (!strcasecmp(command, "getbyte")) {
		rv = g781_read8(offset, &data);
		if (rv < 0)
			return rv;
		ccprintf("Byte at offset 0x%02x is %08b\n", offset, data);
		return rv;
	}

	/* Remaining commands are of the form "g781 set-command offset data" */
	if (argc != 4)
		return EC_ERROR_PARAM_COUNT;

	data = strtoi(argv[3], &e, 0);
	if (*e)
		return EC_ERROR_PARAM3;

	if (!strcasecmp(command, "settemp")) {
		ccprintf("Setting 0x%02x to %dC\n", offset, data);
		rv = g781_set_temp(offset, data);
	} else if (!strcasecmp(command, "setbyte")) {
		ccprintf("Setting 0x%02x to 0x%02x\n", offset, data);
		rv = g781_write8(offset, data);
	} else
		return EC_ERROR_PARAM1;

	return rv;
}
DECLARE_CONSOLE_COMMAND(g781, command_g781,
	"[settemp|setbyte <offset> <value>] or [getbyte <offset>]. "
	"Temps in Celsius.",
	"Print g781 temp sensor status or set parameters.", NULL);
