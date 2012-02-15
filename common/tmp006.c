/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TMP006 temperature sensor module for Chrome EC */

#include "tmp006.h"
#include "temp_sensor.h"
#include "board.h"
#include "uart.h"
#include "util.h"
#include "console.h"
#include "task.h"
#include "fpu.h"
#include "math.h"
#include "i2c.h"

/* Defined in board_temp_sensor.c. */
extern const struct tmp006_t tmp006_sensors[TMP006_COUNT];

struct tmp006_data_t {
	/* Object voltage */
	int v;
	/* The last four die temperature value. Used as a circular buffer. */
	int t[4];
	/* The index of the current value in the dir temperature array. */
	int tidx;
};

static struct tmp006_data_t tmp006_data[TMP006_COUNT];

static int tmp006_read_die_temp(int idx)
{
	int pidx = (tmp006_data[idx].tidx - 1) & 0x3;
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
	float Tdie, Vobj, S0;
	float Tx, S, Vos, Vx, fv, Tobj, T4;
	int Tobj_i;

	enable_fpu();

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

	disable_fpu(Tobj_i);

	return Tobj_i;
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

	v = tmp006_correct_object_voltage(
		t,
		tmp006_data[idx].t[(pidx + 3) & 3],
		tmp006_data[idx].t[(pidx + 2) & 3],
		tmp006_data[idx].t[(pidx + 1) & 3],
		v);

	/* TODO: Calibrate the sensitivity factor. */
	return tmp006_calculate_object_temp(t, v, 6400) / 100;
}

static int tmp006_poll_sensor(int sensor_id)
{
	int traw, t;
	int vraw, v;
	int rv;
	int addr = tmp006_sensors[sensor_id].addr;
	int idx;

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0x01, &traw);
	if (rv)
		return EC_ERROR_UNKNOWN;
	t = ((int)(int16_t)traw * 100) / 128 + 27300;

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0x00, &vraw);
	if (rv)
		return EC_ERROR_UNKNOWN;
	v = ((int)(int16_t)vraw * 15625) / 100;

	idx = tmp006_data[sensor_id].tidx;
	tmp006_data[sensor_id].t[idx] = t;
	tmp006_data[sensor_id].v = v;
	tmp006_data[sensor_id].tidx = (idx + 1) & 3;

	return EC_SUCCESS;
}

static int tmp006_print(int idx)
{
	int vraw, v;
	int traw, t;
	int rv;
	int d;
	int addr = tmp006_sensors[idx].addr;

	uart_printf("Debug data from %s:\n", tmp006_sensors[idx].name);
	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0xfe, &d);
	if (rv)
		return rv;
	uart_printf("  Manufacturer ID: 0x%04x\n", d);

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0xff, &d);
	uart_printf("  Device ID:       0x%04x\n", d);

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0x02, &d);
	uart_printf("  Config:          0x%04x\n", d);

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0x00, &vraw);
	v = ((int)(int16_t)vraw * 15625) / 100;
	uart_printf("  Voltage:         0x%04x = %d nV\n", vraw, v);

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0x01, &traw);
	t = ((int)(int16_t)traw * 100) / 128;
	uart_printf("  Temperature:     0x%04x = %d.%02d C\n",
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


/*****************************************************************************/
/* Console commands */

/* TMP006 object temperature calculation command.
 * TODO: This command is only for debugging. Remove it when temporal correciton
 *       is done.
 */
static int command_sensor_remote(int argc, char **argv)
{
	char *e;
	int32_t Td2, Vobj9, Sm03;

	if (argc != 4) {
		uart_puts("Usage: tempcorrect <Tdie*100> <Vobj*10^9> <S0*10^11>\n");
		return EC_ERROR_UNKNOWN;
	}

	Td2 = strtoi(argv[1], &e, 0);
	if (e && *e) {
		uart_puts("Bad Tdie.\n");
		return EC_ERROR_UNKNOWN;
	}

	Vobj9 = strtoi(argv[2], &e, 0);
	if (e && *e) {
		uart_puts("Bad Vobj.\n");
		return EC_ERROR_UNKNOWN;
	}

	Sm03 = strtoi(argv[3], &e, 0);
	if (e && *e) {
		uart_puts("Bad S0.\n");
		return EC_ERROR_UNKNOWN;
	}

	uart_printf("%d\n",
		tmp006_calculate_object_temp(Td2, Vobj9, Sm03));

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(tempremote, command_sensor_remote);

static int command_sensor_info(int argc, char **argv)
{
	int i;
	int rv, rv1;

	rv1 = EC_SUCCESS;
	for (i = 0; i < TMP006_COUNT; ++i) {
		rv = tmp006_print(i);
		if (rv != EC_SUCCESS)
			rv1 = rv;
	}

	return rv1;
}
DECLARE_CONSOLE_COMMAND(tmp006, command_sensor_info);
