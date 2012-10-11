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
#include "host_command.h"
#include "i2c.h"
#include "math.h"
#include "task.h"
#include "temp_sensor.h"
#include "tmp006.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_THERMAL, outstr)
#define CPRINTF(format, args...) cprintf(CC_THERMAL, format, ## args)

/* Constants for calculating target object temperatures */
const float A1 = 1.75e-3f;
const float A2 = -1.678e-5f;
const float B0 = -2.94e-5f;
const float B1 = -5.7e-7f;
const float B2 = 4.63e-9f;
const float C2 = 13.4f;

/* Defined in board_temp_sensor.c. */
extern const struct tmp006_t tmp006_sensors[TMP006_COUNT];

/* Flags for tdata->fail */
#define FAIL_INIT        (1 << 0)  /* Just initialized */
#define FAIL_POWER       (1 << 1)  /* Sensor not powered */
#define FAIL_I2C         (1 << 2)  /* I2C communication error */
#define FAIL_NOT_READY   (1 << 3)  /* Data not ready */

struct tmp006_data_t {
	int v;     /* Object voltage */
	int t[4];  /* Circular buffer of last four die temperatures */
	int tidx;  /* Index of the current value in t[] */
	int fail;  /* Fail flags; non-zero if last read failed */
	float s0;  /* Sensitivity factor */
	float b0, b1, b2;  /* Coefficients for self-heating correction */
};

static struct tmp006_data_t tmp006_data[TMP006_COUNT];

/**
 * Check if sensor has power
 *
 * @param idx		Sensor index
 *
 * @return non-zero if sensor has power.
 */
static int tmp006_has_power(int idx)
{
	/* All TMP006 sensors are powered by VS. */
	return gpio_get_level(GPIO_PGOOD_1_8VS);
}

static int tmp006_read_die_temp(const struct tmp006_data_t *tdata,
				int *temp_ptr)
{
	if (tdata->fail)
		return EC_ERROR_UNKNOWN;

	/* Return previous die temperature */
	*temp_ptr = tdata->t[(tdata->tidx - 1) & 0x3] / 100;
	return EC_SUCCESS;
}

/**
 * Calculate the remote object temperature.
 *
 * @param Tdie_i	Die temperature in 1/100 K.
 * @param Vobj_i	Voltage read from register 0. In nV.
 * @param tdata		TMP006 data for this sensor.
 *
 * @return Object temperature in 1/100 K.
 */
static int tmp006_calculate_object_temp(int Tdie_i, int Vobj_i,
					const struct tmp006_data_t *tdata)
{
	float Tdie, Vobj;
	float Tx, S, Vos, Vx, fv, Tobj, T4;
	int Tobj_i;

	Tdie = (float)Tdie_i * 1e-2f;
	Vobj = (float)Vobj_i * 1e-9f;

	/* Calculate according to TMP006 users guide. */
	Tx = Tdie - 298.15f;
	/* S is the sensitivity */
	S = tdata->s0 * (1.0f + A1 * Tx + A2 * Tx * Tx);
	/* Vos is the offset voltage */
	Vos = tdata->b0 + tdata->b1 * Tx + tdata->b2 * Tx * Tx;
	Vx = Vobj - Vos;
	/* fv is Seebeck coefficient f(Vobj) */
	fv = Vx + C2 * Vx * Vx;

	T4 = Tdie * Tdie * Tdie * Tdie + fv / S;
	Tobj = sqrtf(sqrtf(T4));
	Tobj_i = (int32_t)(Tobj * 100.0f);

	return Tobj_i;
}

/**
 * Apply TMP006 temporal correction.
 *
 * @param T1-T4		Four die temperature readings separated by 1s in 1/100K.
 * @param Vobj		Voltage read from register 0, in nV.
 *
 * @return Corrected object voltage in 1/100K.
 */
static int tmp006_correct_object_voltage(int T1, int T2, int T3, int T4,
					 int Vobj)
{
	int Tslope = 3 * T1 + T2 - T3 - 3 * T4;
	return Vobj + 296 * Tslope;
}

static int tmp006_read_object_temp(const struct tmp006_data_t *tdata,
				   int *temp_ptr)
{
	int pidx = (tdata->tidx - 1) & 0x3;
	int t = tdata->t[pidx];
	int v = tdata->v;

	if (tdata->fail)
		return EC_ERROR_UNKNOWN;

	if (!tdata->s0)
		return EC_ERROR_NOT_CALIBRATED;

	v = tmp006_correct_object_voltage(
		t,
		tdata->t[(pidx + 3) & 3],
		tdata->t[(pidx + 2) & 3],
		tdata->t[(pidx + 1) & 3],
		v);

	*temp_ptr = tmp006_calculate_object_temp(t, v, tdata) / 100;

	return EC_SUCCESS;
}

static int tmp006_poll_sensor(int sensor_id)
{
	struct tmp006_data_t *tdata = tmp006_data + sensor_id;
	int traw, t;
	int vraw, v;
	int rv;
	int addr = tmp006_sensors[sensor_id].addr;
	int idx;

	if (!tmp006_has_power(sensor_id)) {
		tdata->fail |= FAIL_POWER;
		return EC_ERROR_UNKNOWN;
	}

	/*
	 * If sensor has just initialized and/or has lost power, wait for
	 * data ready; otherwise, we read garbage data.
	 */
	if (tdata->fail && (FAIL_POWER | FAIL_INIT)) {
		rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0x02, &v);
		if (!(v & 0x80)) {
			tdata->fail |= FAIL_NOT_READY;
			return EC_ERROR_UNKNOWN;
		}
	}

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0x01, &traw);
	if (rv) {
		tdata->fail |= FAIL_I2C;
		return EC_ERROR_UNKNOWN;
	}

	/* Convert temperature from raw to 1/100 K */
	t = ((int)(int16_t)traw * 100) / 128 + 27300;

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr), 0x00, &vraw);
	if (rv) {
		tdata->fail |= FAIL_I2C;
		return EC_ERROR_UNKNOWN;
	}

	/* Convert voltage from raw to nV */
	v = ((int)(int16_t)vraw * 15625) / 100;

	/*
	 * If last read failed, set the entire temperature history to the
	 * current temperature.  This keeps us from making inaccurate temporal
	 * corrections based on stale data.
	 */
	if (tdata->fail) {
		for (idx = 0; idx < 4; idx++)
			tdata->t[idx] = t;
	} else {
		idx = tdata->tidx;
		tdata->t[idx] = t;
		tdata->tidx = (idx + 1) & 3;
	}

	tdata->v = v;
	tdata->fail = 0;

	return EC_SUCCESS;
}

int tmp006_get_val(int idx, int *temp_ptr)
{
	/*
	 * Note: idx is a thermal sensor index, where the top N-1 bits are the
	 * TMP006 index and the bottom bit is (0=die, 1=remote).
	 */
	int tidx = idx >> 1;
	const struct tmp006_data_t *tdata = tmp006_data + tidx;

	if (tdata->fail & FAIL_POWER)
		return EC_ERROR_NOT_POWERED;

	/* Check the low bit to determine which temperature to read. */
	if ((idx & 0x1) == 0)
		return tmp006_read_die_temp(tdata, temp_ptr);
	else
		return tmp006_read_object_temp(tdata, temp_ptr);
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
	int i;

	for (i = 0; i < TMP006_COUNT; ++i) {
		struct tmp006_data_t *tdata = tmp006_data + i;

		/* Report error until we actually read the sensor */
		tdata->fail = FAIL_INIT;

		/* Use defaults for Bn params */
		tdata->b0 = B0;
		tdata->b1 = B1;
		tdata->b2 = B2;
	}

	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_INIT, tmp006_init, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Host commands */

int tmp006_get_calibration(struct host_cmd_handler_args *args)
{
	const struct ec_params_tmp006_get_calibration *p = args->params;
	struct ec_response_tmp006_get_calibration *r = args->response;
	const struct tmp006_data_t *tdata;

	if (p->index >= TMP006_COUNT)
		return EC_RES_INVALID_PARAM;

	tdata = tmp006_data + p->index;

	r->s0 = tdata->s0;
	r->b0 = tdata->b0;
	r->b1 = tdata->b1;
	r->b2 = tdata->b2;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_TMP006_GET_CALIBRATION,
		     tmp006_get_calibration,
		     EC_VER_MASK(0));

int tmp006_set_calibration(struct host_cmd_handler_args *args)
{
	const struct ec_params_tmp006_set_calibration *p = args->params;
	struct tmp006_data_t *tdata;

	if (p->index >= TMP006_COUNT)
		return EC_RES_INVALID_PARAM;

	tdata = tmp006_data + p->index;

	tdata->s0 = p->s0;
	tdata->b0 = p->b0;
	tdata->b1 = p->b1;
	tdata->b2 = p->b2;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_TMP006_SET_CALIBRATION,
		     tmp006_set_calibration,
		     EC_VER_MASK(0));

/*****************************************************************************/
/* Console commands */

/* Print temperature info for a sensor; used by console command. */
static int tmp006_print(int idx)
{
	int vraw, v;
	int traw, t;
	int rv;
	int d;
	int addr = tmp006_sensors[idx].addr;


	ccprintf("Debug data from %s:\n", tmp006_sensors[idx].name);

	if (!tmp006_has_power(idx)) {
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

static int command_sensor_info(int argc, char **argv)
{
	int i;
	int rv, rv1;

	rv1 = EC_SUCCESS;
	for (i = 0; i < TMP006_COUNT; i++) {
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

static int command_t6cal(int argc, char **argv)
{
	struct tmp006_data_t *tdata;
	char *e;
	int v;
	int i;

	if (argc < 2) {
		ccprintf("# Name            S0          b0"
			 "         b1          b2\n");
		for (i = 0; i < TMP006_COUNT; i++) {
			tdata = tmp006_data + i;
			ccprintf("%d %-11s"
				 "%7de-17 %7de-8 %7de-10 %7de-12\n",
				 i, tmp006_sensors[i].name,
				 (int)(tdata->s0 * 1e17f),
				 (int)(tdata->b0 * 1e8f),
				 (int)(tdata->b1 * 1e10f),
				 (int)(tdata->b2 * 1e12f));
		}

		return EC_SUCCESS;
	}

	if (argc != 4)
		return EC_ERROR_PARAM_COUNT;

	i = strtoi(argv[1], &e, 0);
	if (*e || i < 0 || i >= TMP006_COUNT)
		return EC_ERROR_PARAM1;
	tdata = tmp006_data + i;

	v = strtoi(argv[3], &e, 0);
	if (*e)
		return EC_ERROR_PARAM3;

	if (!strcasecmp(argv[2], "s0"))
		tdata->s0 = (float)v * 1e-17f;
	else if (!strcasecmp(argv[2], "b0"))
		tdata->b0 = (float)v * 1e-8f;
	else if (!strcasecmp(argv[2], "b1"))
		tdata->b1 = (float)v * 1e-10f;
	else if (!strcasecmp(argv[2], "b2"))
		tdata->b2 = (float)v * 1e-12f;
	else
		return EC_ERROR_PARAM2;

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(t6cal, command_t6cal,
			"[<index> <coeff_name> <radix>]",
			"Set/print TMP006 calibration",
			NULL);
