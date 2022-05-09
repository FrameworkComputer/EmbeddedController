/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/zephyr.h>
#include <ztest.h>

#include "common.h"
#include "i2c.h"
#include "emul/emul_tcs3400.h"
#include "emul/emul_common_i2c.h"

#include "motion_sense.h"
#include "motion_sense_fifo.h"
#include "driver/als_tcs3400.h"
#include "test/drivers/test_state.h"

#define TCS_ORD			DT_DEP_ORD(DT_NODELABEL(tcs_emul))
#define TCS_CLR_SENSOR_ID	SENSOR_ID(DT_NODELABEL(tcs3400_clear))
#define TCS_RGB_SENSOR_ID	SENSOR_ID(DT_NODELABEL(tcs3400_rgb))
#define TCS_INT_EVENT		\
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(SENSOR_ID(DT_ALIAS(tcs3400_int)))

/** How accurate comparision of rgb sensors should be */
#define V_EPS		8

/** Test initialization of light sensor driver and device */
ZTEST_USER(tcs3400, test_tcs_init)
{
	struct motion_sensor_t *ms, *ms_rgb;
	struct i2c_emul *emul;

	emul = tcs_emul_get(TCS_ORD);
	ms = &motion_sensors[TCS_CLR_SENSOR_ID];
	ms_rgb = &motion_sensors[TCS_RGB_SENSOR_ID];

	/* RGB sensor initialization is always successful */
	zassert_equal(EC_SUCCESS, ms_rgb->drv->init(ms_rgb), NULL);

	/* Fail init on communication errors */
	i2c_common_emul_set_read_fail_reg(emul, I2C_COMMON_EMUL_FAIL_ALL_REG);
	zassert_equal(EC_ERROR_INVAL, ms->drv->init(ms), NULL);
	i2c_common_emul_set_read_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Fail on bad ID */
	tcs_emul_set_reg(emul, TCS_I2C_ID, 0);
	zassert_equal(EC_ERROR_ACCESS_DENIED, ms->drv->init(ms), NULL);
	/* Restore ID */
	tcs_emul_set_reg(emul, TCS_I2C_ID,
			 DT_STRING_TOKEN(DT_NODELABEL(tcs_emul), device_id));

	/* Test successful init. ATIME and AGAIN should be changed on init */
	zassert_equal(EC_SUCCESS, ms->drv->init(ms), NULL);
	zassert_equal(TCS_DEFAULT_ATIME,
		      tcs_emul_get_reg(emul, TCS_I2C_ATIME), NULL);
	zassert_equal(TCS_DEFAULT_AGAIN,
		      tcs_emul_get_reg(emul, TCS_I2C_CONTROL), NULL);
}

/** Test if read function leaves device in correct mode to accuire data */
ZTEST_USER(tcs3400, test_tcs_read)
{
	struct motion_sensor_t *ms;
	struct i2c_emul *emul;
	uint8_t enable;
	intv3_t v;

	emul = tcs_emul_get(TCS_ORD);
	ms = &motion_sensors[TCS_CLR_SENSOR_ID];

	/* Test error on writing registers */
	i2c_common_emul_set_write_fail_reg(emul, TCS_I2C_ATIME);
	zassert_equal(EC_ERROR_INVAL, ms->drv->read(ms, v), NULL);
	i2c_common_emul_set_write_fail_reg(emul, TCS_I2C_CONTROL);
	zassert_equal(EC_ERROR_INVAL, ms->drv->read(ms, v), NULL);
	i2c_common_emul_set_write_fail_reg(emul, TCS_I2C_ENABLE);
	zassert_equal(EC_ERROR_INVAL, ms->drv->read(ms, v), NULL);
	i2c_common_emul_set_write_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test starting read with calibration */
	tcs_emul_set_reg(emul, TCS_I2C_ATIME, 0);
	tcs_emul_set_reg(emul, TCS_I2C_CONTROL, 0);
	tcs_emul_set_reg(emul, TCS_I2C_ENABLE, 0);
	zassert_equal(EC_SUCCESS, ms->drv->perform_calib(ms, 1), NULL);
	zassert_equal(EC_RES_IN_PROGRESS, ms->drv->read(ms, v), NULL);
	zassert_equal(TCS_CALIBRATION_ATIME,
		      tcs_emul_get_reg(emul, TCS_I2C_ATIME), NULL);
	zassert_equal(TCS_CALIBRATION_AGAIN,
		      tcs_emul_get_reg(emul, TCS_I2C_CONTROL), NULL);
	enable = tcs_emul_get_reg(emul, TCS_I2C_ENABLE);
	zassert_true(enable & TCS_I2C_ENABLE_POWER_ON, NULL);
	zassert_true(enable & TCS_I2C_ENABLE_ADC_ENABLE, NULL);
	zassert_true(enable & TCS_I2C_ENABLE_INT_ENABLE, NULL);

	/* Test starting read without calibration */
	tcs_emul_set_reg(emul, TCS_I2C_ATIME, 0);
	tcs_emul_set_reg(emul, TCS_I2C_CONTROL, 0);
	tcs_emul_set_reg(emul, TCS_I2C_ENABLE, 0);
	zassert_equal(EC_SUCCESS, ms->drv->perform_calib(ms, 0), NULL);
	zassert_equal(EC_RES_IN_PROGRESS, ms->drv->read(ms, v), NULL);
	enable = tcs_emul_get_reg(emul, TCS_I2C_ENABLE);
	zassert_true(enable & TCS_I2C_ENABLE_POWER_ON, NULL);
	zassert_true(enable & TCS_I2C_ENABLE_ADC_ENABLE, NULL);
	zassert_true(enable & TCS_I2C_ENABLE_INT_ENABLE, NULL);
}

/** Check if FIFO for RGB and clear sensor is empty */
static void check_fifo_empty_f(struct motion_sensor_t *ms,
			       struct motion_sensor_t *ms_rgb, int line)
{
	struct ec_response_motion_sensor_data vector;
	uint16_t size;

	/* Read all data committed to FIFO */
	while (motion_sense_fifo_read(sizeof(vector), 1, &vector, &size)) {
		/* Ignore timestamp frames */
		if (vector.flags == MOTIONSENSE_SENSOR_FLAG_TIMESTAMP) {
			continue;
		}

		if (ms - motion_sensors == vector.sensor_num) {
			zassert_unreachable(
				"Unexpected frame for clear sensor @line: %d",
				line);
		}

		if (ms_rgb - motion_sensors == vector.sensor_num) {
			zassert_unreachable(
				"Unexpected frame for rgb sensor @line: %d",
				line);
		}
	}
}
#define check_fifo_empty(ms, ms_rgb)		\
	check_fifo_empty_f(ms, ms_rgb, __LINE__)

/**
 * Test different conditions where irq handler fail or commit no data
 * to fifo
 */
ZTEST_USER(tcs3400, test_tcs_irq_handler_fail)
{
	struct motion_sensor_t *ms, *ms_rgb;
	struct i2c_emul *emul;
	uint32_t event;

	emul = tcs_emul_get(TCS_ORD);
	ms = &motion_sensors[TCS_CLR_SENSOR_ID];
	ms_rgb = &motion_sensors[TCS_RGB_SENSOR_ID];

	/* Fail on wrong event */
	event = 0x1234 & ~TCS_INT_EVENT;
	zassert_equal(EC_ERROR_NOT_HANDLED, ms->drv->irq_handler(ms, &event),
		      NULL);
	check_fifo_empty(ms, ms_rgb);

	event = TCS_INT_EVENT;
	/* Test error on reading status */
	i2c_common_emul_set_read_fail_reg(emul, TCS_I2C_STATUS);
	zassert_equal(EC_ERROR_INVAL, ms->drv->irq_handler(ms, &event), NULL);
	i2c_common_emul_set_read_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);
	check_fifo_empty(ms, ms_rgb);

	/* Test fail on changing device power state */
	i2c_common_emul_set_write_fail_reg(emul, TCS_I2C_ENABLE);
	zassert_equal(EC_ERROR_INVAL, ms->drv->irq_handler(ms, &event), NULL);
	i2c_common_emul_set_write_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);
	check_fifo_empty(ms, ms_rgb);

	/* Test that no data is committed when status is 0 */
	tcs_emul_set_reg(emul, TCS_I2C_STATUS, 0);
	zassert_equal(EC_SUCCESS, ms->drv->irq_handler(ms, &event), NULL);
	check_fifo_empty(ms, ms_rgb);
}

/**
 * Check if last data committed to FIFO for RGB and clear sensor equals to
 * expected value.
 */
static void check_fifo_f(struct motion_sensor_t *ms,
			 struct motion_sensor_t *ms_rgb,
			 int *exp_v, int eps, int line)
{
	struct ec_response_motion_sensor_data vector;
	uint16_t size;
	int ret_v[4] = {-1, -1, -1, -1};
	int i;

	/* Read all data committed to FIFO */
	while (motion_sense_fifo_read(sizeof(vector), 1, &vector, &size)) {
		/* Ignore timestamp frames */
		if (vector.flags == MOTIONSENSE_SENSOR_FLAG_TIMESTAMP) {
			continue;
		}

		/* Get clear frame */
		if (ms - motion_sensors == vector.sensor_num) {
			ret_v[0] = vector.udata[0];
		}

		/* Get rgb frame */
		if (ms_rgb - motion_sensors == vector.sensor_num) {
			ret_v[1] = vector.udata[0];
			ret_v[2] = vector.udata[1];
			ret_v[3] = vector.udata[2];
		}
	}

	if (ret_v[0] == -1) {
		zassert_unreachable("No frame for clear sensor, line %d", line);
	}

	if (ret_v[1] == -1) {
		zassert_unreachable("No frame for rgb sensor, line %d", line);
	}

	/* Compare with last committed data */
	for (i = 0; i < 4; i++) {
		zassert_within(exp_v[i], ret_v[i], eps,
			"Expected [%d; %d; %d; %d], got [%d; %d; %d; %d]; line: %d",
			exp_v[0], exp_v[1], exp_v[2], exp_v[3],
			ret_v[0], ret_v[1], ret_v[2], ret_v[3], line);
	}
}
#define check_fifo(ms, ms_rgb, exp_v, eps)		\
	check_fifo_f(ms, ms_rgb, exp_v, eps, __LINE__)

/** Test calibration mode reading of light sensor values */
ZTEST_USER(tcs3400, test_tcs_read_calibration)
{
	struct motion_sensor_t *ms, *ms_rgb;
	struct i2c_emul *emul;
	uint32_t event = TCS_INT_EVENT;
	int emul_v[4];
	int exp_v[4];
	intv3_t v;

	emul = tcs_emul_get(TCS_ORD);
	ms = &motion_sensors[TCS_CLR_SENSOR_ID];
	ms_rgb = &motion_sensors[TCS_RGB_SENSOR_ID];

	/* Need to be set to collect all data in FIFO */
	ms->oversampling_ratio = 1;
	ms_rgb->oversampling_ratio = 1;
	/* Enable calibration mode */
	zassert_equal(EC_SUCCESS, ms->drv->perform_calib(ms, 1), NULL);
	/* Setup AGAIN and ATIME for calibration */
	zassert_equal(EC_RES_IN_PROGRESS, ms->drv->read(ms, v), NULL);

	/* Test data that are in calibration range */
	exp_v[0] = 12;
	exp_v[1] = 123;
	exp_v[2] = 1234;
	exp_v[3] = 12345;
	/*
	 * Emulator value is with gain 64, while expected value is
	 * with gain 16
	 */
	emul_v[0] = exp_v[0] * 64 / 16;
	emul_v[1] = exp_v[1] * 64 / 16;
	emul_v[2] = exp_v[2] * 64 / 16;
	emul_v[3] = exp_v[3] * 64 / 16;
	tcs_emul_set_val(emul, TCS_EMUL_C, emul_v[0]);
	tcs_emul_set_val(emul, TCS_EMUL_R, emul_v[1]);
	tcs_emul_set_val(emul, TCS_EMUL_G, emul_v[2]);
	tcs_emul_set_val(emul, TCS_EMUL_B, emul_v[3]);
	/* Set status to show valid data */
	tcs_emul_set_reg(emul, TCS_I2C_STATUS, TCS_I2C_STATUS_RGBC_VALID);

	zassert_equal(EC_SUCCESS, ms->drv->irq_handler(ms, &event), NULL);
	/* In calibration mode check for exact match */
	check_fifo(ms, ms_rgb, exp_v, 1);

	/* Test data that are outside of calibration range */
	exp_v[0] = 0;
	exp_v[1] = UINT16_MAX;
	exp_v[2] = UINT16_MAX;
	exp_v[3] = 213;
	/*
	 * Emulator value is with gain 64, while expected value is
	 * with gain 16
	 */
	emul_v[0] = 0;
	emul_v[1] = exp_v[1] * 64 / 16;
	emul_v[2] = (UINT16_MAX + 23) * 64 / 16;
	emul_v[3] = exp_v[3] * 64 / 16;
	tcs_emul_set_val(emul, TCS_EMUL_C, emul_v[0]);
	tcs_emul_set_val(emul, TCS_EMUL_R, emul_v[1]);
	tcs_emul_set_val(emul, TCS_EMUL_G, emul_v[2]);
	tcs_emul_set_val(emul, TCS_EMUL_B, emul_v[3]);
	/* Set status to show valid data */
	tcs_emul_set_reg(emul, TCS_I2C_STATUS, TCS_I2C_STATUS_RGBC_VALID);

	zassert_equal(EC_SUCCESS, ms->drv->irq_handler(ms, &event), NULL);
	/* In calibration mode check for exact match */
	check_fifo(ms, ms_rgb, exp_v, 1);
}

/**
 * Set emulator internal value using expected output value returned by
 * the driver. First element of expected vector is IR value used in
 * calculations. Based on that clear light value is calculated.
 * First element of expected vector is updated by this function.
 */
static void set_emul_val_from_exp(int *exp_v, uint16_t *scale,
				  struct i2c_emul *emul)
{
	int emul_v[4];
	int ir;

	/* We use exp_v[0] as IR value */
	ir = exp_v[0];
	/* Driver will return lux value as calculated blue light value */
	exp_v[0] = exp_v[2];

	/*
	 * Driver takes care of different ATIME and AGAIN value, so expected
	 * value is always normalized to ATIME 256 and AGAIN 16. Convert it
	 * to internal emulator value (ATIME 256, AGAIN 64) and add expected IR
	 * value. Clear light is the sum of rgb light and IR component.
	 */
	emul_v[1] = (exp_v[1] + ir) * 64 / 16;
	emul_v[2] = (exp_v[2] + ir) * 64 / 16;
	emul_v[3] = (exp_v[3] + ir) * 64 / 16;
	emul_v[0] = (exp_v[1] + exp_v[2] + exp_v[3] + ir) * 64 / 16;

	/* Apply scale, driver should divide by this value */
	emul_v[0] = SENSOR_APPLY_SCALE(emul_v[0], scale[0]);
	emul_v[1] = SENSOR_APPLY_SCALE(emul_v[1], scale[1]);
	emul_v[2] = SENSOR_APPLY_SCALE(emul_v[2], scale[2]);
	emul_v[3] = SENSOR_APPLY_SCALE(emul_v[3], scale[3]);

	/* Set emulator values */
	tcs_emul_set_val(emul, TCS_EMUL_C, emul_v[0]);
	tcs_emul_set_val(emul, TCS_EMUL_R, emul_v[1]);
	tcs_emul_set_val(emul, TCS_EMUL_G, emul_v[2]);
	tcs_emul_set_val(emul, TCS_EMUL_B, emul_v[3]);
}

/** Test normal mode reading of light sensor values */
ZTEST_USER(tcs3400, test_tcs_read_xyz)
{
	struct motion_sensor_t *ms, *ms_rgb;
	struct i2c_emul *emul;
	uint32_t event = TCS_INT_EVENT;
	/* Expected data to test: IR, R, G, B */
	int exp_v[][4] = {
		{200,	1110,	870,	850},
		{300,	1110,	10000,	8500},
		{600,	50000,	40000,	30000},
		{1000,	3000,	40000,	2000},
		{1000,	65000,	65000,	65000},
		{100,	214,	541,	516},
		{143,	2141,	5414,	5163},
		{100,	50000,	40000,	30000},
		{1430,	2141,	5414,	5163},
		{10000,	50000,	40000,	30000},
		{10000,	214,	541,	516},
		{15000,	50000,	40000,	30000},
	};
	uint16_t scale[4] = {
		MOTION_SENSE_DEFAULT_SCALE, MOTION_SENSE_DEFAULT_SCALE,
		MOTION_SENSE_DEFAULT_SCALE, MOTION_SENSE_DEFAULT_SCALE
	};
	int i, test;
	intv3_t v;

	emul = tcs_emul_get(TCS_ORD);
	ms = &motion_sensors[TCS_CLR_SENSOR_ID];
	ms_rgb = &motion_sensors[TCS_RGB_SENSOR_ID];

	/* Need to be set to collect all data in FIFO */
	ms->oversampling_ratio = 1;
	ms_rgb->oversampling_ratio = 1;
	/* Disable calibration mode */
	zassert_equal(EC_SUCCESS, ms->drv->perform_calib(ms, 0), NULL);
	/* Setup AGAIN and ATIME for normal mode */
	zassert_equal(EC_RES_IN_PROGRESS, ms->drv->read(ms, v), NULL);

	/* Test different data in supported range */
	for (test = 0; test < ARRAY_SIZE(exp_v); test++) {
		set_emul_val_from_exp(exp_v[test], scale, emul);

		/* Run few times to allow driver change gain */
		for (i = 0; i < 5; i++) {
			tcs_emul_set_reg(emul, TCS_I2C_STATUS,
					 TCS_I2C_STATUS_RGBC_VALID);
			zassert_equal(EC_SUCCESS,
				      ms->drv->irq_handler(ms, &event), NULL);
		}
		check_fifo(ms, ms_rgb, exp_v[test], V_EPS);
	}

	/* Test data that are outside of supported range */
	exp_v[0][0] = 3000;
	exp_v[0][1] = UINT16_MAX;
	exp_v[0][2] = UINT16_MAX * 32;
	exp_v[0][3] = 200;
	set_emul_val_from_exp(exp_v[0], scale, emul);

	/* Run few times to allow driver change gain */
	for (i = 0; i < 10; i++) {
		tcs_emul_set_reg(emul, TCS_I2C_STATUS,
				 TCS_I2C_STATUS_RGBC_VALID);
		zassert_equal(EC_SUCCESS, ms->drv->irq_handler(ms, &event),
			      NULL);
	}
	/*
	 * If saturation value is exceeded on any rgb sensor, than data
	 * shouldn't be committed to FIFO.
	 */
	check_fifo_empty(ms, ms_rgb);
}

/**
 * Test getting and setting scale of light sensor. Checks if collected values
 * are scaled properly.
 */
ZTEST_USER(tcs3400, test_tcs_scale)
{
	struct motion_sensor_t *ms, *ms_rgb;
	struct i2c_emul *emul;
	uint32_t event = TCS_INT_EVENT;
	/* Expected data to test: IR, R, G, B */
	int exp_v[][4] = {
		{200,	1110,	870,	850},
		{300,	1110,	10000,	8500},
		{600,	5000,	4000,	3000},
		{100,	3000,	4000,	2000},
		{100,	1000,	1000,	1000},
	};
	/* Scale for each test */
	uint16_t exp_scale[][4] = {
		{MOTION_SENSE_DEFAULT_SCALE, MOTION_SENSE_DEFAULT_SCALE,
		 MOTION_SENSE_DEFAULT_SCALE, MOTION_SENSE_DEFAULT_SCALE},
		{MOTION_SENSE_DEFAULT_SCALE + 300,
		 MOTION_SENSE_DEFAULT_SCALE + 300,
		 MOTION_SENSE_DEFAULT_SCALE + 300,
		 MOTION_SENSE_DEFAULT_SCALE + 300},
		{MOTION_SENSE_DEFAULT_SCALE - 300,
		 MOTION_SENSE_DEFAULT_SCALE - 300,
		 MOTION_SENSE_DEFAULT_SCALE - 300,
		 MOTION_SENSE_DEFAULT_SCALE - 300},
		{MOTION_SENSE_DEFAULT_SCALE + 345,
		 MOTION_SENSE_DEFAULT_SCALE - 5423,
		 MOTION_SENSE_DEFAULT_SCALE - 30,
		 MOTION_SENSE_DEFAULT_SCALE + 400},
		{MOTION_SENSE_DEFAULT_SCALE - 345,
		 MOTION_SENSE_DEFAULT_SCALE + 5423,
		 MOTION_SENSE_DEFAULT_SCALE + 30,
		 MOTION_SENSE_DEFAULT_SCALE - 400},
		{MOTION_SENSE_DEFAULT_SCALE, MOTION_SENSE_DEFAULT_SCALE,
		 MOTION_SENSE_DEFAULT_SCALE, MOTION_SENSE_DEFAULT_SCALE}
	};
	uint16_t scale[3];
	int16_t temp;
	int i, test;
	intv3_t v;

	emul = tcs_emul_get(TCS_ORD);
	ms = &motion_sensors[TCS_CLR_SENSOR_ID];
	ms_rgb = &motion_sensors[TCS_RGB_SENSOR_ID];

	/* Need to be set to collect all data in FIFO */
	ms->oversampling_ratio = 1;
	ms_rgb->oversampling_ratio = 1;
	/* Disable calibration mode */
	zassert_equal(EC_SUCCESS, ms->drv->perform_calib(ms, 0), NULL);
	/* Setup AGAIN and ATIME for normal mode */
	zassert_equal(EC_RES_IN_PROGRESS, ms->drv->read(ms, v), NULL);

	/* Test different data in supported range */
	for (test = 0; test < ARRAY_SIZE(exp_v); test++) {
		/* Set and test clear sensor scale */
		zassert_equal(EC_SUCCESS,
			      ms->drv->set_scale(ms, exp_scale[test], 0),
			      "test %d", test);
		zassert_equal(EC_SUCCESS,
			      ms->drv->get_scale(ms, scale, &temp),
			      "test %d", test);
		zassert_equal((int16_t)EC_MOTION_SENSE_INVALID_CALIB_TEMP, temp,
			      "test %d, %d", test, temp);
		zassert_equal(exp_scale[test][0], scale[0], "test %d", test);

		/* Set and test RGB sensor scale */
		zassert_equal(EC_SUCCESS, ms_rgb->drv->set_scale(ms_rgb,
						&(exp_scale[test][1]), 0),
			      "test %d", test);
		zassert_equal(EC_SUCCESS,
			      ms_rgb->drv->get_scale(ms_rgb, scale, &temp),
			      "test %d", test);
		zassert_equal((int16_t)EC_MOTION_SENSE_INVALID_CALIB_TEMP, temp,
			      "test %d", test);
		zassert_equal(exp_scale[test][1], scale[0], "test %d", test);
		zassert_equal(exp_scale[test][2], scale[1], "test %d", test);
		zassert_equal(exp_scale[test][3], scale[2], "test %d", test);

		set_emul_val_from_exp(exp_v[test], exp_scale[test], emul);

		/* Run few times to allow driver change gain */
		for (i = 0; i < 5; i++) {
			tcs_emul_set_reg(emul, TCS_I2C_STATUS,
					 TCS_I2C_STATUS_RGBC_VALID);
			zassert_equal(EC_SUCCESS,
				      ms->drv->irq_handler(ms, &event), NULL);
		}
		check_fifo(ms, ms_rgb, exp_v[test], V_EPS);
	}

	/* Test fail if scale equals 0 */
	scale[0] = 0;
	scale[1] = MOTION_SENSE_DEFAULT_SCALE;
	scale[2] = MOTION_SENSE_DEFAULT_SCALE;
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_scale(ms, scale, 0), NULL);

	zassert_equal(EC_ERROR_INVAL, ms_rgb->drv->set_scale(ms_rgb, scale, 0),
		      NULL);
	scale[0] = MOTION_SENSE_DEFAULT_SCALE;
	scale[1] = 0;
	scale[2] = MOTION_SENSE_DEFAULT_SCALE;
	zassert_equal(EC_ERROR_INVAL, ms_rgb->drv->set_scale(ms_rgb, scale, 0),
		      NULL);
	scale[0] = MOTION_SENSE_DEFAULT_SCALE;
	scale[1] = MOTION_SENSE_DEFAULT_SCALE;
	scale[2] = 0;
	zassert_equal(EC_ERROR_INVAL, ms_rgb->drv->set_scale(ms_rgb, scale, 0),
		      NULL);
}

/** Test setting and getting data rate of light sensor */
ZTEST_USER(tcs3400, test_tcs_data_rate)
{
	struct motion_sensor_t *ms, *ms_rgb;
	struct i2c_emul *emul;
	uint8_t enable;

	emul = tcs_emul_get(TCS_ORD);
	ms = &motion_sensors[TCS_CLR_SENSOR_ID];
	/* RGB sensor doesn't set rate, but return rate of clear sesnor */
	ms_rgb = &motion_sensors[TCS_RGB_SENSOR_ID];

	/* Test fail on reading device power state */
	i2c_common_emul_set_read_fail_reg(emul, TCS_I2C_ENABLE);
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_data_rate(ms, 0, 0), NULL);
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_data_rate(ms, 0, 1), NULL);
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_data_rate(ms, 100, 0), NULL);
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_data_rate(ms, 100, 1), NULL);
	i2c_common_emul_set_read_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test setting 0 rate disables device */
	zassert_equal(EC_SUCCESS, ms->drv->set_data_rate(ms, 0, 0), NULL);
	zassert_equal(0, tcs_emul_get_reg(emul, TCS_I2C_ENABLE), NULL);
	zassert_equal(0, ms->drv->get_data_rate(ms), NULL);
	zassert_equal(0, ms_rgb->drv->get_data_rate(ms_rgb), NULL);

	zassert_equal(EC_SUCCESS, ms->drv->set_data_rate(ms, 0, 1), NULL);
	zassert_equal(0, tcs_emul_get_reg(emul, TCS_I2C_ENABLE), NULL);
	zassert_equal(0, tcs_emul_get_reg(emul, TCS_I2C_ENABLE), NULL);
	zassert_equal(0, ms->drv->get_data_rate(ms), NULL);
	zassert_equal(0, ms_rgb->drv->get_data_rate(ms_rgb), NULL);


	/* Test setting non-zero rate enables device */
	zassert_equal(EC_SUCCESS, ms->drv->set_data_rate(ms, 100, 0), NULL);
	enable = tcs_emul_get_reg(emul, TCS_I2C_ENABLE);
	zassert_true(enable & TCS_I2C_ENABLE_POWER_ON, NULL);
	zassert_true(enable & TCS_I2C_ENABLE_ADC_ENABLE, NULL);
	zassert_true(enable & TCS_I2C_ENABLE_INT_ENABLE, NULL);
	zassert_equal(100, ms->drv->get_data_rate(ms), NULL);
	zassert_equal(100, ms_rgb->drv->get_data_rate(ms_rgb), NULL);

	zassert_equal(EC_SUCCESS, ms->drv->set_data_rate(ms, 100, 1), NULL);
	enable = tcs_emul_get_reg(emul, TCS_I2C_ENABLE);
	zassert_true(enable & TCS_I2C_ENABLE_POWER_ON, NULL);
	zassert_true(enable & TCS_I2C_ENABLE_ADC_ENABLE, NULL);
	zassert_true(enable & TCS_I2C_ENABLE_INT_ENABLE, NULL);
	zassert_equal(100, ms->drv->get_data_rate(ms), NULL);
	zassert_equal(100, ms_rgb->drv->get_data_rate(ms_rgb), NULL);

	/* Test RGB sensor doesn't change data rate */
	zassert_equal(EC_SUCCESS, ms_rgb->drv->set_data_rate(ms_rgb, 300, 0),
		      NULL);
	zassert_equal(100, ms->drv->get_data_rate(ms), NULL);
	zassert_equal(100, ms_rgb->drv->get_data_rate(ms_rgb), NULL);

	zassert_equal(EC_SUCCESS, ms_rgb->drv->set_data_rate(ms_rgb, 300, 1),
		      NULL);
	zassert_equal(100, ms->drv->get_data_rate(ms), NULL);
	zassert_equal(100, ms_rgb->drv->get_data_rate(ms_rgb), NULL);
}

/** Test set range function of clear and RGB sensors */
ZTEST_USER(tcs3400, test_tcs_set_range)
{
	struct motion_sensor_t *ms, *ms_rgb;
	struct i2c_emul *emul;

	emul = tcs_emul_get(TCS_ORD);
	ms = &motion_sensors[TCS_CLR_SENSOR_ID];
	ms_rgb = &motion_sensors[TCS_RGB_SENSOR_ID];

	/* RGB sensor doesn't set anything */
	zassert_equal(EC_SUCCESS, ms_rgb->drv->set_range(ms_rgb, 1, 0), NULL);

	/* Clear sensor doesn't change anything on device to set range */
	zassert_equal(EC_SUCCESS, ms->drv->set_range(ms, 0x12300, 1), NULL);
	zassert_equal(0x12300, ms->current_range, NULL);

	zassert_equal(EC_SUCCESS, ms->drv->set_range(ms, 0x10000, 0), NULL);
	zassert_equal(0x10000, ms->current_range, NULL);
}

ZTEST_SUITE(tcs3400, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
