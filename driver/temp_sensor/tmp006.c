/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TMP006 temperature sensor module for Chrome EC */

#include "common.h"
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
#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ## args)

/*
 * Alg 0 was what's in the TMP006 User's Guide.  Alg 1 is Alg 0, but with
 * some filters applied to the Tdie input and Tobj output (see
 * crosbug.com/p/32260).
 */
#define ALGORITHM_NUM 1
#define ALGORITHM_PARAMS 12

/* Flags for tdata->fail */
#define FAIL_INIT        (1 << 0)  /* Just initialized */
#define FAIL_POWER       (1 << 1)  /* Sensor not powered */
#define FAIL_I2C         (1 << 2)  /* I2C communication error */
#define FAIL_NOT_READY   (1 << 3)  /* Data not ready */

/* State and conversion factors to track for each sensor */
struct tmp006_data_t {
	/* chip info */
	int16_t v_raw;		 /* TMP006_REG_VOBJ */
	int16_t t_raw0;		 /* TMP006_REG_TDIE */
	int fail;		 /* Fail flags; non-zero if last read failed */
	/* calibration params */
	float s0, a1, a2;	 /* Sensitivity factors */
	float b0, b1, b2;	 /* Self-heating correction */
	float c2;		 /* Seebeck effect */
	float d0, d1, ds;	 /* Tdie filter and slope adjustment */
	float e0, e1;		 /* Tobj output filter */
	/* FIR filter stages */
	float tdie1, tobj1;
};
static struct tmp006_data_t tmp006_data[TMP006_COUNT];

/* Default state and conversion factors */
static const struct tmp006_data_t tmp006_data_default = {
	.fail = FAIL_INIT,

	/* Alg 0 params from User's Guide */
	.s0 = 0.0f,				/* zero == "uncalibrated" */
	.a1 = 1.75e-3f,
	.a2 = -1.678e-5f,
	.b0 = -2.94e-5f,
	.b1 = -5.7e-7f,
	.b2 = 4.63e-9f,
	.c2 = 13.4f,

	/* Additional Alg 1 filter params */
	.d0 = 0.2f,
	.d1 = 0.8f,
	.ds = 1.48e-4,
	.e0 = 0.1f,
	.e1 = 0.9f,
};

static int tmp006_has_power(int idx)
{
#ifdef CONFIG_TEMP_SENSOR_POWER_GPIO
	return gpio_get_level(CONFIG_TEMP_SENSOR_POWER_GPIO);
#else
	return 1;
#endif
}

static void tmp006_poll_sensor(int sensor_id)
{
	struct tmp006_data_t *tdata = tmp006_data + sensor_id;
	int t, v, rv;
	int addr = tmp006_sensors[sensor_id].addr;

	/* Invalidate the filter history if there is any error */
	if (tdata->fail) {
		tdata->tdie1 = 0.0f;
		tdata->tobj1 = 0.0;
	}

	if (!tmp006_has_power(sensor_id)) {
		tdata->fail |= FAIL_POWER;
		return;
	}

	/*
	 * If sensor has just initialized and/or has lost power, wait for
	 * data ready; otherwise, we read garbage data.
	 */
	if (tdata->fail & (FAIL_POWER | FAIL_INIT)) {
		rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr),
				TMP006_REG_CONFIG, &v);
		if (rv) {
			tdata->fail |= FAIL_I2C;
			return;
		} else if (!(v & 0x80)) {
			/* Bit 7 is the Data Ready bit */
			tdata->fail |= FAIL_NOT_READY;
			return;
		}
	}

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr),
			TMP006_REG_TDIE, &t);
	if (rv) {
		tdata->fail |= FAIL_I2C;
		return;
	}

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr),
			TMP006_REG_VOBJ, &v);
	if (rv) {
		tdata->fail |= FAIL_I2C;
		return;
	}

	tdata->t_raw0 = t;
	tdata->v_raw = v;

	tdata->fail = 0;
}

/*****************************************************************************/
/* Hooks */

static void tmp006_init(void)
{
	int i;

	for (i = 0; i < TMP006_COUNT; ++i)
		tmp006_data[i] = tmp006_data_default;
}
DECLARE_HOOK(HOOK_INIT, tmp006_init, HOOK_PRIO_DEFAULT);

static void tmp006_poll(void)
{
	int i;

	for (i = 0; i < TMP006_COUNT; ++i)
		tmp006_poll_sensor(i);
}
DECLARE_HOOK(HOOK_SECOND, tmp006_poll, HOOK_PRIO_TEMP_SENSOR);

/*****************************************************************************/
/* Interface to the rest of the EC */

/* This just returns Tdie */
static int tmp006_read_die_temp_k(const struct tmp006_data_t *tdata,
				  int *temp_ptr)
{
	if (tdata->fail)
		return EC_ERROR_UNKNOWN;

	/* Tdie reg is signed 1/128 degrees C, resolution 1/32 degrees */
	*temp_ptr = (int)tdata->t_raw0 / 128 + 273;
	return EC_SUCCESS;
}

/*
 * This uses Tdie and Vobj and a bunch of magic parameters to calulate the
 * object temperature, Tobj.
 */
static int tmp006_read_object_temp_k(struct tmp006_data_t *tdata,
				     int *temp_ptr)
{
	float tdie, vobj;
	float tx, s, vos, vx, fv, tobj, t4;
	float tdie_filtered, tdie_slope, tobj_filtered;

	if (tdata->fail)
		return EC_ERROR_UNKNOWN;

	if (!tdata->s0)
		return EC_ERROR_NOT_CALIBRATED;

	/* Tdie reg is signed 1/128 degrees C, resolution 1/32 degrees
	 * We need degrees K */
	tdie = (float)tdata->t_raw0 / 128.0f + 273.15f;
	/* Vobj reg is signed int, LSB = 156.25 nV
	 * We need volts */
	vobj = (float)tdata->v_raw / 156.25f * 1e-9f;

	/* Alg1: apply filter to tdie. If tdie1 is 0K, initialize it. */
	if (tdata->tdie1 == 0.0f)
		tdata->tdie1 = tdie;
	tdie_filtered = tdata->d0 * tdie + tdata->d1 * tdata->tdie1;
	tdie_slope = tdie - tdie_filtered;
	/* Remember the current Tdie for next time */
	tdata->tdie1 = tdie;

	/* Calculate according to TMP006 users guide. */
	tx = tdie - 298.15f;
	/* s is the sensitivity */
	s = tdata->s0 * (1.0f + tdata->a1 * tx + tdata->a2 * tx * tx);
	/* vos is the offset voltage */
	vos = tdata->b0 + tdata->b1 * tx + tdata->b2 * tx * tx;
	/* Alg1: use Tdie FIR here */
	vx = vobj - vos + tdie_slope * tdata->ds;
	/* fv is Seebeck coefficient f(vobj) */
	fv = vx + tdata->c2 * vx * vx;

	t4 = tdie * tdie * tdie * tdie + fv / s;
	tobj = sqrtf(sqrtf(t4));

	/* Alg1: apply another filter on the calculated tobj. */
	if (tdata->tobj1 == 0.0f)
		tdata->tobj1 = tobj;

	tobj_filtered = tdata->e0 * tobj + tdata->e1 * tdata->tobj1;
	tdata->tobj1 = tobj;

	/* return integer degrees K */
	*temp_ptr = tobj_filtered;

	return EC_SUCCESS;
}

int tmp006_get_val(int idx, int *temp_ptr)
{
	/*
	 * Note: idx is a thermal sensor index, where the top N-1 bits are the
	 * TMP006 index and the bottom bit is (0=die, 1=remote).
	 */
	int tidx = idx >> 1;
	struct tmp006_data_t *tdata = tmp006_data + tidx;

	if (tdata->fail & FAIL_POWER) {
		/*
		 * Sensor isn't powered, or hasn't successfully provided data
		 * since being powered.  Keep reporting not-powered until
		 * we get good data (which will clear FAIL_POWER) or there is
		 * an I2C error.
		 */
		return (tdata->fail & FAIL_I2C) ? EC_ERROR_UNKNOWN :
			EC_ERROR_NOT_POWERED;
	}

	/* Check the low bit to determine which temperature to read. */
	if ((idx & 0x1) == 0)
		return tmp006_read_die_temp_k(tdata, temp_ptr);
	else
		return tmp006_read_object_temp_k(tdata, temp_ptr);
}

/*****************************************************************************/
/* Host commands */

int tmp006_get_calibration(struct host_cmd_handler_args *args)
{
	const struct ec_params_tmp006_get_calibration *p = args->params;
	struct ec_response_tmp006_get_calibration_v1 *r1 = args->response;
	const struct tmp006_data_t *tdata;

	if (p->index >= TMP006_COUNT)
		return EC_RES_INVALID_PARAM;

	tdata = tmp006_data + p->index;

	r1->algorithm = ALGORITHM_NUM;
	r1->num_params = ALGORITHM_PARAMS;
	r1->val[0]  = tdata->s0;
	r1->val[1]  = tdata->a1;
	r1->val[2]  = tdata->a2;
	r1->val[3]  = tdata->b0;
	r1->val[4]  = tdata->b1;
	r1->val[5]  = tdata->b2;
	r1->val[6]  = tdata->c2;
	r1->val[7]  = tdata->d0;
	r1->val[8]  = tdata->d1;
	r1->val[9]  = tdata->ds;
	r1->val[10] = tdata->e0;
	r1->val[11] = tdata->e1;

	args->response_size = sizeof(*r1) +
		r1->num_params * sizeof(r1->val[0]);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_TMP006_GET_CALIBRATION,
		     tmp006_get_calibration,
		     EC_VER_MASK(1));

int tmp006_set_calibration(struct host_cmd_handler_args *args)
{
	const struct ec_params_tmp006_set_calibration_v1 *p1 = args->params;
	struct tmp006_data_t *tdata;

	if (p1->index >= TMP006_COUNT)
		return EC_RES_INVALID_PARAM;

	/* We only have one algorithm today */
	if (p1->algorithm != ALGORITHM_NUM ||
	    p1->num_params != ALGORITHM_PARAMS)
		return EC_RES_INVALID_PARAM;

	tdata = tmp006_data + p1->index;

	tdata->s0 = p1->val[0];
	tdata->a1 = p1->val[1];
	tdata->a2 = p1->val[2];
	tdata->b0 = p1->val[3];
	tdata->b1 = p1->val[4];
	tdata->b2 = p1->val[5];
	tdata->c2 = p1->val[6];
	tdata->d0 = p1->val[7];
	tdata->d1 = p1->val[8];
	tdata->ds = p1->val[9];
	tdata->e0 = p1->val[10];
	tdata->e1 = p1->val[11];

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_TMP006_SET_CALIBRATION,
		     tmp006_set_calibration,
		     EC_VER_MASK(1));

int tmp006_get_raw(struct host_cmd_handler_args *args)
{
	const struct ec_params_tmp006_get_raw *p = args->params;
	struct ec_response_tmp006_get_raw *r = args->response;
	const struct tmp006_data_t *tdata;

	if (p->index >= TMP006_COUNT)
		return EC_RES_INVALID_PARAM;

	tdata = tmp006_data + p->index;

	/* Vobj reg is signed int, LSB = 156.25 nV
	 * response units are nV */
	r->v = ((int)tdata->v_raw * 15625) / 100;

	/* Tdie reg is signed 1/128 degrees C, resolution 1/32 degrees
	 * response units are 1/100 degrees K */
	r->t = ((int)tdata->t_raw0 * 100) / 128 + 27315;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_TMP006_GET_RAW,
		     tmp006_get_raw,
		     EC_VER_MASK(0));

/*****************************************************************************/
/* Console commands */

/**
 * Print temperature info for a sensor; used by console command.
 */
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

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr),
			TMP006_REG_MANUFACTURER_ID, &d);
	if (rv)
		return rv;
	ccprintf("  Manufacturer ID: 0x%04x\n", d);

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr),
			TMP006_REG_DEVICE_ID, &d);
	ccprintf("  Device ID:       0x%04x\n", d);

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr),
			TMP006_REG_CONFIG, &d);
	ccprintf("  Config:          0x%04x\n", d);

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr),
			TMP006_REG_VOBJ, &vraw);

	v = ((int)vraw * 15625) / 100;
	ccprintf("  Voltage:         0x%04x = %d nV\n", vraw, v);

	rv = i2c_read16(TMP006_PORT(addr), TMP006_REG(addr),
			TMP006_REG_TDIE, &traw);
	t = (int)traw;
	ccprintf("  Temperature:     0x%04x = %d.%02d C\n",
		 traw, t / 128, t > 0 ? t % 128 : 128 - (t % 128));

	return EC_SUCCESS;
}

static int command_sensor_info(int argc, char **argv)
{
	int i;
	int rv, rv1;
	int a = 0, b = TMP006_COUNT;

	if (argc > 1) {
		char *e = 0;
		i = strtoi(argv[1], &e, 0);
		if (*e || i < 0 || i >= TMP006_COUNT)
			return EC_ERROR_PARAM1;
		a = i;
		b = i + 1;
	}

	rv1 = EC_SUCCESS;
	for (i = a; i < b; i++) {
		rv = tmp006_print(i);
		if (rv != EC_SUCCESS)
			rv1 = rv;
		cflush();
	}

	return rv1;
}
DECLARE_CONSOLE_COMMAND(tmp006, command_sensor_info,
			"[ <index> ]",
			"Print TMP006 sensors",
			NULL);

/* Disable the t6cal command until/unless we have FP support in printf */
#if 0
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
#endif
