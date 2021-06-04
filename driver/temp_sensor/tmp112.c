/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TMP112 temperature sensor module for Chrome EC */

#include "common.h"
#include "console.h"
#include "tmp112.h"
#include "i2c.h"
#include "hooks.h"
#include "util.h"

#define TMP112_RESOLUTION 12
#define TMP112_SHIFT1 (16 - TMP112_RESOLUTION)
#define TMP112_SHIFT2 (TMP112_RESOLUTION - 8)

#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ## args)

static int temp_val_local[TMP112_COUNT];

static int raw_read16(int sensor, const int offset, int *data_ptr)
{
#ifdef CONFIG_I2C_BUS_MAY_BE_UNPOWERED
	/*
	 * Don't try to read if the port is unpowered
	 */
	if (!board_is_i2c_port_powered(tmp112_sensors[sensor].i2c_port))
		return EC_ERROR_NOT_POWERED;
#endif
	return i2c_read16(tmp112_sensors[sensor].i2c_port,
			  tmp112_sensors[sensor].i2c_addr_flags,
			  offset, data_ptr);
}

static int raw_write16(int sensor, const int offset, int data)
{
#ifdef CONFIG_I2C_BUS_MAY_BE_UNPOWERED
	/*
	 * Don't try to write if the port is unpowered
	 */
	if (!board_is_i2c_port_powered(tmp112_sensors[sensor].i2c_port))
		return EC_ERROR_NOT_POWERED;
#endif
	return i2c_write16(tmp112_sensors[sensor].i2c_port,
			   tmp112_sensors[sensor].i2c_addr_flags,
			   offset, data);
}

static int get_temp(int sensor, int *temp_ptr)
{
	int rv;
	int temp_raw = 0;

	rv = raw_read16(sensor, TMP112_REG_TEMP, &temp_raw);
	if (rv)
		return rv;

	*temp_ptr = (int)(int16_t)temp_raw;
	return EC_SUCCESS;
}

static inline int tmp112_reg_to_c(int16_t reg)
{
	int tmp;

	tmp = (((reg >> TMP112_SHIFT1) * 1000 ) >> TMP112_SHIFT2);

	return tmp / 1000;
}

int tmp112_get_val(int idx, int *temp_ptr)
{
	if (idx >= TMP112_COUNT)
		return EC_ERROR_INVAL;

	*temp_ptr = temp_val_local[idx];
	return EC_SUCCESS;
}

static void tmp112_poll(void)
{
	int s;
	int temp_c = 0;

	for (s = 0; s < TMP112_COUNT; s++) {
		if (get_temp(s, &temp_c) == EC_SUCCESS)
			temp_val_local[s] = C_TO_K(tmp112_reg_to_c(temp_c));
	}
}
DECLARE_HOOK(HOOK_SECOND, tmp112_poll, HOOK_PRIO_TEMP_SENSOR);

void tmp112_init(void)
{
	int tmp, s, rv;
	int set_mask, clr_mask;

	/* 12 bit mode */
	set_mask = (3 << 5);

	/* not oneshot mode */
	clr_mask = BIT(7);

	for (s = 0; s < TMP112_COUNT; s++) {
		rv = raw_read16(s, TMP112_REG_CONF, &tmp);
		if (rv != EC_SUCCESS) {
			CPRINTS("TMP112-%d: Failed to init (rv %d)", s, rv);
			continue;
		}
		raw_write16(s, TMP112_REG_CONF, (tmp & ~clr_mask) | set_mask);
	}
}
DECLARE_HOOK(HOOK_INIT, tmp112_init, HOOK_PRIO_DEFAULT);
