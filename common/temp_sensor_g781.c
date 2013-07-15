/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* G781 temperature sensor module for Chrome EC */

#include "common.h"
#include "console.h"
#include "i2c.h"
#include "temp_sensor_g781.h"

int g781_get_val(int idx, int *temp_ptr)
{
	int command;
	int rv;
	int temp_raw = 0;

	if (!board_g781_has_power())
		return EC_ERROR_NOT_POWERED;

	switch (idx) {
	case 0:
		command = G781_TEMP_LOCAL;
		break;
	case 1:
		command = G781_TEMP_REMOTE;
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}

	rv = i2c_read8(I2C_PORT_THERMAL, G781_I2C_ADDR, command, &temp_raw);

	if (rv < 0)
		return rv;

	/* Negative numbers are 2's compliment with sign bit 7 */
	if (temp_raw & (1 << 7))
		temp_raw = ~(~temp_raw & 0xff) + 1;

	/* Temperature from sensor is in degrees Celsius */
	*temp_ptr = temp_raw + 273;
	return EC_SUCCESS;
}
