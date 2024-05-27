/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* OTI502 temperature sensor module for Chrome EC */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "i2c.h"
#include "oti502.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_THERMAL, format, ##args)

static int temp_val_ambient; /* Ambient is chip temperature*/
static int temp_val_object; /* Object is IR temperature */

static int oti502_read_block(const int offset, uint8_t *data, int len)
{
	return i2c_read_block(I2C_PORT_THERMAL, OTI502_I2C_ADDR_FLAGS, offset,
			      data, len);
}

int oti502_get_val(int idx, int *temp_ptr)
{
	switch (idx) {
	case OTI502_IDX_AMBIENT:
		*temp_ptr = temp_val_ambient;
		break;
	case OTI502_IDX_OBJECT:
		*temp_ptr = temp_val_object;
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

static void temp_sensor_poll(void)
{
	uint8_t temp_val[6];

	memset(temp_val, 0, sizeof(temp_val));

	oti502_read_block(0x80, temp_val, sizeof(temp_val));

	if (temp_val[2] >= 0x80) {
		/* Treat temperature as 0 degree C if temperature is negative*/
		temp_val_ambient = 0;
		CPRINTF("Temperature ambient is negative !\n");
	} else {
		temp_val_ambient = ((temp_val[1] << 8) + temp_val[0]) / 200;
		temp_val_ambient = C_TO_K(temp_val_ambient);
	}

	if (temp_val[5] >= 0x80) {
		/* Treat temperature as 0 degree C if temperature is negative*/
		temp_val_object = 0;
		CPRINTF("Temperature object is negative !\n");
	} else {
		temp_val_object = ((temp_val[4] << 8) + temp_val[5]) / 200;
		temp_val_object = C_TO_K(temp_val_object);
	}
}
DECLARE_HOOK(HOOK_SECOND, temp_sensor_poll, HOOK_PRIO_TEMP_SENSOR);
