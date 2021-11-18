/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PCT2075 temperature sensor module for Chrome EC */

#include "common.h"
#include "console.h"
#include "pct2075.h"
#include "i2c.h"
#include "hooks.h"
#include "math_util.h"
#include "util.h"

#define PCT2075_RESOLUTION 11
#define PCT2075_SHIFT1 (16 - PCT2075_RESOLUTION)
#define PCT2075_SHIFT2 (PCT2075_RESOLUTION - 8)

#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ## args)

static int temp_mk_local[PCT2075_COUNT];

static int raw_read16(int sensor, const int offset, int *data_ptr)
{
#ifdef CONFIG_I2C_BUS_MAY_BE_UNPOWERED
	/*
	 * Don't try to read if the port is unpowered
	 */
	if (!board_is_i2c_port_powered(pct2075_sensors[sensor].i2c_port))
		return EC_ERROR_NOT_POWERED;
#endif
	return i2c_read16(pct2075_sensors[sensor].i2c_port,
			  pct2075_sensors[sensor].i2c_addr_flags,
			  offset, data_ptr);
}

static int get_reg_temp(int sensor, int *temp_ptr)
{
	int temp_raw = 0;

	RETURN_ERROR(raw_read16(sensor, PCT2075_REG_TEMP, &temp_raw));

	*temp_ptr = (int)(int16_t)temp_raw;
	return EC_SUCCESS;
}

static inline int pct2075_reg_to_mk(int16_t reg)
{
	int temp_mc;

	temp_mc = (((reg >> PCT2075_SHIFT1) * 1000) >> PCT2075_SHIFT2);

	return MILLI_CELSIUS_TO_MILLI_KELVIN(temp_mc);
}

int pct2075_get_val_k(int idx, int *temp_k_ptr)
{
	if (idx >= PCT2075_COUNT)
		return EC_ERROR_INVAL;

	*temp_k_ptr = MILLI_KELVIN_TO_KELVIN(temp_mk_local[idx]);
	return EC_SUCCESS;
}

int pct2075_get_val_mk(int idx, int *temp_mk_ptr)
{
	if (idx >= PCT2075_COUNT)
		return EC_ERROR_INVAL;

	*temp_mk_ptr = temp_mk_local[idx];
	return EC_SUCCESS;
}

static void pct2075_poll(void)
{
	int s;
	int temp_reg = 0;

	for (s = 0; s < PCT2075_COUNT; s++) {
		if (get_reg_temp(s, &temp_reg) == EC_SUCCESS)
			temp_mk_local[s] = pct2075_reg_to_mk(temp_reg);
	}
}
DECLARE_HOOK(HOOK_SECOND, pct2075_poll, HOOK_PRIO_TEMP_SENSOR);

void pct2075_init(void)
{
/* Incase we need to initialize somthing */
}
DECLARE_HOOK(HOOK_INIT, pct2075_init, HOOK_PRIO_DEFAULT);
