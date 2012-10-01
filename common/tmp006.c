/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TMP006 temperature sensor module for Chrome EC */

#include "board.h"
#include "config.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "math.h"
#include "task.h"
#include "temp_sensor.h"
#include "tmp006.h"
#include "util.h"

/* Defined in board_temp_sensor.c. */
extern const struct tmp006_t tmp006_sensors[TMP006_COUNT];

struct tmp006_data_t {
	/* Object voltage */
	int v;
	/* The last four die temperature value. Used as a circular buffer. */
	int t[4];
	/* The index of the current value in the dir temperature array. */
	int tidx;
	/* Fail bit: 1 if last read fail. 0 if ok. */
	int fail;
};

static struct tmp006_data_t tmp006_data[TMP006_COUNT];

static int tmp006_read_die_temp(int idx)
{
	int pidx = (tmp006_data[idx].tidx - 1) & 0x3;
	if (tmp006_data[idx].fail == 1)
		return -1;
	return tmp006_data[idx].t[pidx] / 100;
}

/* Calculate the remote object temperature.
 * Parameters:
 *     Tdie: Die temperature in 1/100 K.
 *     Vobj: Voltage read from register 0. In nV.
 *     S0:   Sensitivity factor in 1e-17.
 * Return:
 *     Object temperature in 1/100 K.
 */
static int tmp006_calculate_object_temp(int Tdie_i, int Vobj_i, int S0_i)
{
#ifdef CONFIG_FPU
	float Tdie, Vobj, S0;
	float Tx, S, Vos, Vx, fv, Tobj, T4;
	int Tobj_i;

	Tdie = (float)Tdie_i * 1e-2f;
	Vobj = (float)Vobj_i * 1e-9f;
	S0 = (float)S0_i * 1e-17f;

	/* Calculate according to TMP006 users guide. */
	Tx = Tdie - 298.15f;
	/* S is the sensitivity */
	S = S0 * (1.0f + 1.75e-3f * Tx - 1.678e-5f * Tx * Tx);
	/* Vos is the offset voltage */
	Vos = -2.94e-5f - 5.7e-7f * Tx + 4.63e-9f * Tx * Tx;
	Vx = Vobj - Vos;
	/* fv is Seebeck coefficient f(Vobj) */
	fv = Vx + 13.4f * Vx * Vx;

	T4 = Tdie * Tdie * Tdie * Tdie + fv / S;
	Tobj = sqrtf(sqrtf(T4));
	Tobj_i = (int32_t)(Tobj * 100.0f);

	return Tobj_i;
#else
	/* This is the fixed-point version of object temperature calculation.
	 * Should be accurate but it is hard to prevent and debug
	 * overflow/underflow problem. Only use this version if there is no
	 * FPU support.
	 * Division is delayed when possible to preserve precision, but should
	 * not cause overflow.
	 * Assuming Tdie is between 200K and 400K, and S0 between 3e-14 and
	 * 9e-14, the maximum value during the calculation should be less than
	 * (1 << 30), which fits in int32_t.
	 */
	int32_t Tx, S19, Vos, Vx, fv9, ub, lb;

	Tx = Tdie - 29815;
	/* S19 is the sensitivity multipled by 1e19 */
	S19 = S0 * (100000 + 175 * Tx / 100 -
			1678 * Tx / 100 * Tx / 100000) / 1000;
	/* Vos is the offset voltage in nV */
	Vos = -29400 - 570 * Tx / 100 + 463 * Tx / 100 * Tx / 10000;
	Vx = Vobj - Vos;
	/* fv9 is Seebeck coefficient f(Vobj) multipled by 1e9 */
	fv9 = Vx + 134 * Vx / 100000 * Vx / 100000;

	/* The last step in the calculation involves square root, so we use
	 * binary search.
	 * Assuming the object temperature is between 200K and 400K, the search
	 * should take at most 14 iterations.
	 */
	ub = 40000;
	lb = 20000;
	while (lb != ub) {
		int32_t t, rhs, lhs;

		t = (ub + lb) / 2;
		lhs = t / 100 * t / 10000 * t / 10000 * (S19/100) / 1000 * t;
		rhs = Tdie / 100 * Tdie / 10000 * Tdie / 10000 * (S19/100) /
			1000 * Tdie + fv9 * 1000;
		if (lhs > rhs)
			ub = t;
		else
			lb = t + 1;
	}

	return ub;
#endif /* CONFIG_FPU */
}

/* Temporal Correction
 * Parameters:
 *     T1-T4: Four die temperature readings separated by 1s in 1/100K.
 *     v:     Voltage read from register 0. In nV.
 * Return:
 *     Corrected object voltage in 1/100K.
 */
static int tmp006_correct_object_voltage(int T1,
					      int T2,
					      int T3,
					      int T4,
					      int Vobj)
{
	int Tslope = 3 * T1 + T2 - T3 - 3 * T4;
	return Vobj + 296 * Tslope;
}

static int tmp006_read_object_temp(int idx)
{
	int pidx = (tmp006_data[idx].tidx - 1) & 0x3;
	int t = tmp006_data[idx].t[pidx];
	int v = tmp006_data[idx].v;

	if (tmp006_data[idx].fail)
		return -1;

	v = tmp006_correct_object_voltage(
		t,
		tmp006_data[idx].t[(pidx + 3) & 3],
		tmp006_data[idx].t[(pidx + 2) & 3],
		tmp006_data[idx].t[(pidx + 1) & 3],
		v);

	/* TODO: Calibrate the sensitivity factor. */
	return tmp006_calculate_object_temp(t, v,
			tmp006_sensors[idx].sens) / 100;
}

static int tmp006_poll_sensor(int sensor_id)
{
	int traw, t;
	int vraw, v;
	int rv;
	int addr = tmp006_sensors[sensor_id].addr;
	int idx;

	/* TODO: For now, all TMP006 sensors are powered by VS. Modify this
	 *       if we have different design.
	 */
	if (gpio_get_level(GPIO_PGOOD_1_8VS) == 0) {
		tmp006_data[sensor_id].fail = 1;
		return EC_ERROR_UNKNOWN;
	}

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0x01, &traw);
	if (rv) {
		tmp006_data[sensor_id].fail = 1;
		return EC_ERROR_UNKNOWN;
	}
	t = ((int)(int16_t)traw * 100) / 128 + 27300;

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0x00, &vraw);
	if (rv) {
		tmp006_data[sensor_id].fail = 1;
		return EC_ERROR_UNKNOWN;
	}
	v = ((int)(int16_t)vraw * 15625) / 100;

	idx = tmp006_data[sensor_id].tidx;
	tmp006_data[sensor_id].t[idx] = t;
	tmp006_data[sensor_id].v = v;
	tmp006_data[sensor_id].tidx = (idx + 1) & 3;
	tmp006_data[sensor_id].fail = 0;

	return EC_SUCCESS;
}


/* Print temperature info for a sensor; used by console command. */
static int tmp006_print(int idx)
{
	int vraw, v;
	int traw, t;
	int rv;
	int d;
	int addr = tmp006_sensors[idx].addr;


	ccprintf("Debug data from %s:\n", tmp006_sensors[idx].name);

	/* TODO: For now, all TMP006 sensors are powered by VS. Modify this
	 *       if we have different design.
	 */
	if (gpio_get_level(GPIO_PGOOD_1_8VS) == 0) {
		ccputs("Sensor powered off.\n");
		return EC_ERROR_UNKNOWN;
	}

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0xfe, &d);
	if (rv)
		return rv;
	ccprintf("  Manufacturer ID: 0x%04x\n", d);

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0xff, &d);
	ccprintf("  Device ID:       0x%04x\n", d);

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0x02, &d);
	ccprintf("  Config:          0x%04x\n", d);

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0x00, &vraw);
	v = ((int)(int16_t)vraw * 15625) / 100;
	ccprintf("  Voltage:         0x%04x = %d nV\n", vraw, v);

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0x01, &traw);
	t = ((int)(int16_t)traw * 100) / 128;
	ccprintf("  Temperature:     0x%04x = %d.%02d C\n",
		 traw, t / 100, t > 0 ? t % 100 : 100 - (t % 100));

	return EC_SUCCESS;
}


int tmp006_get_val(int idx)
{
	/* Check the low bit to determine which temperature to read. */
	if ((idx & 0x1) == 0)
		return tmp006_read_die_temp(idx >> 1);
	else
		return tmp006_read_object_temp(idx >> 1);
}


int tmp006_poll(void)
{
	int i;
	int rv;
	int rv1 = EC_SUCCESS;

	for (i = 0; i < TMP006_COUNT; ++i) {
		rv = tmp006_poll_sensor(i);
		if (rv != EC_SUCCESS)
			rv1 = rv;
	}

	return rv1;
}

static int tmp006_init(void)
{
	int i, j;

	/*
	 * Set temperature value to 27 C and we will update it later when
	 * polled by temperature sensor module.
	 */
	for (i = 0; i < TMP006_COUNT; ++i) {
		for (j = 0; j < 4; ++j)
			tmp006_data[i].t[j] = 30000; /* 27 C */
		tmp006_data[i].tidx = 0;
		/* TODO(victoryang): Default value for V? */
	}

	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_INIT, tmp006_init, HOOK_PRIO_DEFAULT);


/*****************************************************************************/
/* Console commands */

static int command_sensor_info(int argc, char **argv)
{
	int i;
	int rv, rv1;

	rv1 = EC_SUCCESS;
	for (i = 0; i < TMP006_COUNT; ++i) {
		rv = tmp006_print(i);
		if (rv != EC_SUCCESS)
			rv1 = rv;
		cflush();
	}

	return rv1;
}
DECLARE_CONSOLE_COMMAND(tmp006, command_sensor_info,
			NULL,
			"Print TMP006 sensors",
			NULL);
