/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr.h>
#include <ztest.h>

#include "common.h"
#include "i2c.h"
#include "emul/emul_bma255.h"

#include "accelgyro.h"
#include "motion_sense.h"
#include "driver/accel_bma2x2.h"

/** How accurate comparision of vectors should be. */
#define V_EPS 8

#define EMUL_LABEL DT_NODELABEL(bma_emul)

#define BMA_ORD DT_DEP_ORD(EMUL_LABEL)

/** Mutex for test motion sensor  */
static mutex_t sensor_mutex;

/** Rotation used in some tests */
static const mat33_fp_t test_rotation = {
	{ 0, FLOAT_TO_FP(1), 0},
	{ FLOAT_TO_FP(-1), 0, 0},
	{ 0, 0, FLOAT_TO_FP(-1)}
};

/** Rotate given vector by test rotation */
void rotate_int3v_by_test_rotation(int16_t *v)
{
	int16_t t;

	t = v[0];
	v[0] = -v[1];
	v[1] = t;
	v[2] = -v[2];
}

static struct accelgyro_saved_data_t acc_data;

/** Mock minimal motion sensor setup required for bma2x2 driver test */
static struct motion_sensor_t ms = {
	.name = "bma_emul",
	.type = MOTIONSENSE_TYPE_ACCEL,
	.drv = &bma2x2_accel_drv,
	.mutex = &sensor_mutex,
	.drv_data = &acc_data,
	.port = NAMED_I2C(accel),
	.i2c_spi_addr_flags = DT_REG_ADDR(EMUL_LABEL),
	.rot_standard_ref = NULL,
	.current_range = 0,
};

/** Set emulator offset values to vector of three int16_t */
static void set_emul_offset(struct i2c_emul *emul, int16_t *offset)
{
	bma_emul_set_off(emul, BMA_EMUL_AXIS_X, offset[0]);
	bma_emul_set_off(emul, BMA_EMUL_AXIS_Y, offset[1]);
	bma_emul_set_off(emul, BMA_EMUL_AXIS_Z, offset[2]);
}

/** Save emulator offset values to vector of three int16_t */
static void get_emul_offset(struct i2c_emul *emul, int16_t *offset)
{
	offset[0] = bma_emul_get_off(emul, BMA_EMUL_AXIS_X);
	offset[1] = bma_emul_get_off(emul, BMA_EMUL_AXIS_Y);
	offset[2] = bma_emul_get_off(emul, BMA_EMUL_AXIS_Z);
}

/** Set emulator accelerometer values to vector of three int16_t */
static void set_emul_acc(struct i2c_emul *emul, int16_t *acc)
{
	bma_emul_set_acc(emul, BMA_EMUL_AXIS_X, acc[0]);
	bma_emul_set_acc(emul, BMA_EMUL_AXIS_Y, acc[1]);
	bma_emul_set_acc(emul, BMA_EMUL_AXIS_Z, acc[2]);
}

/** Convert accelerometer read to units used by emulator */
static void drv_acc_to_emul(intv3_t drv, int range, int16_t *out)
{
	const int scale = MOTION_SCALING_FACTOR / BMA_EMUL_1G;

	out[0] = drv[0] * range / scale;
	out[1] = drv[1] * range / scale;
	out[2] = drv[2] * range / scale;
}

/** Compare two vectors of three int16_t */
static void compare_int3v_f(int16_t *exp_v, int16_t *v, int line)
{
	int i;

	for (i = 0; i < 3; i++) {
		zassert_within(exp_v[i], v[i], V_EPS,
			"Expected [%d; %d; %d], got [%d; %d; %d]; line: %d",
			exp_v[0], exp_v[1], exp_v[2], v[0], v[1], v[2], line);
	}
}
#define compare_int3v(exp_v, v) compare_int3v_f(exp_v, v, __LINE__)

/** Data for reset fail function */
struct reset_func_data {
	/** Fail for given attempts */
	int fail_attempts;
	/** Do not fail for given attempts */
	int ok_before_fail;
	/** Reset register value after given attempts */
	int reset_value;
};

/**
 * Custom emulator function used in init test. It returns cmd soft when reset
 * register is accessed data.reset_value times. Error is returned after
 * accessing register data.ok_before_fail times. Error is returned during next
 * data.fail_attempts times.
 */
static int emul_read_reset(struct i2c_emul *emul, int reg, void *data)
{
	struct reset_func_data *d = data;

	if (reg != BMA2x2_RST_ADDR) {
		return 1;
	}

	if (d->reset_value > 0) {
		d->reset_value--;
		bma_emul_set_reg(emul, BMA2x2_RST_ADDR, BMA2x2_CMD_SOFT_RESET);
	} else {
		bma_emul_set_reg(emul, BMA2x2_RST_ADDR, 0);
	}

	if (d->ok_before_fail > 0) {
		d->ok_before_fail--;
		return 1;
	}

	if (d->fail_attempts > 0) {
		d->fail_attempts--;
		return -EIO;
	}

	return 1;
}

/**
 * Test get offset with and without rotation. Also test behaviour on I2C error.
 */
static void test_bma_get_offset(void)
{
	struct i2c_emul *emul;
	int16_t ret_offset[3];
	int16_t exp_offset[3];
	int16_t temp;

	emul = bma_emul_get(BMA_ORD);

	/* Test fail on each axis */
	bma_emul_set_read_fail_reg(emul, BMA2x2_OFFSET_X_AXIS_ADDR);
	zassert_equal(-EIO, ms.drv->get_offset(&ms, ret_offset, &temp), NULL);
	bma_emul_set_read_fail_reg(emul, BMA2x2_OFFSET_Y_AXIS_ADDR);
	zassert_equal(-EIO, ms.drv->get_offset(&ms, ret_offset, &temp), NULL);
	bma_emul_set_read_fail_reg(emul, BMA2x2_OFFSET_Z_AXIS_ADDR);
	zassert_equal(-EIO, ms.drv->get_offset(&ms, ret_offset, &temp), NULL);

	/* Do not fail on read */
	bma_emul_set_read_fail_reg(emul, BMA_EMUL_NO_FAIL_REG);

	/* Set emulator offset */
	exp_offset[0] = BMA_EMUL_1G / 10;
	exp_offset[1] = BMA_EMUL_1G / 20;
	exp_offset[2] = -(int)BMA_EMUL_1G / 30;
	set_emul_offset(emul, exp_offset);
	/* Disable rotation */
	ms.rot_standard_ref = NULL;

	/* Test get offset without rotation */
	zassert_equal(EC_SUCCESS, ms.drv->get_offset(&ms, ret_offset, &temp),
		      NULL);
	zassert_equal(temp, (int16_t)EC_MOTION_SENSE_INVALID_CALIB_TEMP, NULL);
	compare_int3v(exp_offset, ret_offset);

	/* Setup rotation and rotate expected offset */
	ms.rot_standard_ref = &test_rotation;
	rotate_int3v_by_test_rotation(exp_offset);

	/* Test get offset with rotation */
	zassert_equal(EC_SUCCESS, ms.drv->get_offset(&ms, ret_offset, &temp),
		      NULL);
	zassert_equal(temp, (int16_t)EC_MOTION_SENSE_INVALID_CALIB_TEMP, NULL);
	compare_int3v(exp_offset, ret_offset);
}

/**
 * Test set offset with and without rotation. Also test behaviour on I2C error.
 */
static void test_bma_set_offset(void)
{
	struct i2c_emul *emul;
	int16_t ret_offset[3];
	int16_t exp_offset[3];
	int16_t temp = 0;

	emul = bma_emul_get(BMA_ORD);

	/* Test fail on each axis */
	bma_emul_set_write_fail_reg(emul, BMA2x2_OFFSET_X_AXIS_ADDR);
	zassert_equal(-EIO, ms.drv->set_offset(&ms, exp_offset, temp), NULL);
	bma_emul_set_write_fail_reg(emul, BMA2x2_OFFSET_Y_AXIS_ADDR);
	zassert_equal(-EIO, ms.drv->set_offset(&ms, exp_offset, temp), NULL);
	bma_emul_set_write_fail_reg(emul, BMA2x2_OFFSET_Z_AXIS_ADDR);
	zassert_equal(-EIO, ms.drv->set_offset(&ms, exp_offset, temp), NULL);

	/* Do not fail on write */
	bma_emul_set_write_fail_reg(emul, BMA_EMUL_NO_FAIL_REG);

	/* Set input offset */
	exp_offset[0] = BMA_EMUL_1G / 10;
	exp_offset[1] = BMA_EMUL_1G / 20;
	exp_offset[2] = -(int)BMA_EMUL_1G / 30;
	/* Disable rotation */
	ms.rot_standard_ref = NULL;

	/* Test set offset without rotation */
	zassert_equal(EC_SUCCESS, ms.drv->set_offset(&ms, exp_offset, temp),
		      NULL);
	get_emul_offset(emul, ret_offset);
	compare_int3v(exp_offset, ret_offset);

	/* Setup rotation and rotate input for set_offset function */
	ms.rot_standard_ref = &test_rotation;
	ret_offset[0] = exp_offset[0];
	ret_offset[1] = exp_offset[1];
	ret_offset[2] = exp_offset[2];
	rotate_int3v_by_test_rotation(ret_offset);

	/* Test set offset with rotation */
	zassert_equal(EC_SUCCESS, ms.drv->set_offset(&ms, ret_offset, temp),
		      NULL);
	get_emul_offset(emul, ret_offset);
	compare_int3v(exp_offset, ret_offset);
}

/*
 * Try to set range and check if expected range was set in driver and in
 * emulator.
 */
static void check_set_range_f(struct i2c_emul *emul, int range, int rnd,
			      int exp_range, int line)
{
	uint8_t exp_range_reg;
	uint8_t range_reg;

	zassert_equal(EC_SUCCESS, ms.drv->set_range(&ms, range, rnd),
		      "set_range failed; line: %d", line);
	zassert_equal(exp_range, ms.current_range,
		      "Expected range %d, got %d; line %d",
		      exp_range, ms.current_range, line);
	range_reg = bma_emul_get_reg(emul, BMA2x2_RANGE_SELECT_ADDR);
	range_reg &= BMA2x2_RANGE_SELECT_MSK;

	switch (exp_range) {
	case 2:
		exp_range_reg = BMA2x2_RANGE_2G;
		break;
	case 4:
		exp_range_reg = BMA2x2_RANGE_4G;
		break;
	case 8:
		exp_range_reg = BMA2x2_RANGE_8G;
		break;
	case 16:
		exp_range_reg = BMA2x2_RANGE_16G;
		break;
	default:
		/* Unknown expected range */
		zassert_unreachable(
			"Expected range %d not supported by device; line %d",
			exp_range, line);
		return;
	}

	zassert_equal(exp_range_reg, range_reg,
		      "Expected range reg 0x%x, got 0x%x; line %d",
		      exp_range_reg, range_reg, line);
}
#define check_set_range(emul, range, rnd, exp_range)	\
	check_set_range_f(emul, range, rnd, exp_range, __LINE__)

/** Test set range with and without I2C errors. */
static void test_bma_set_range(void)
{
	struct i2c_emul *emul;
	int start_range;

	emul = bma_emul_get(BMA_ORD);

	/* Setup starting range, shouldn't be changed on error */
	start_range = 2;
	ms.current_range = start_range;
	bma_emul_set_reg(emul, BMA2x2_RANGE_SELECT_ADDR, BMA2x2_RANGE_2G);
	/* Setup emulator fail on read */
	bma_emul_set_read_fail_reg(emul, BMA2x2_RANGE_SELECT_ADDR);

	/* Test fail on read */
	zassert_equal(-EIO, ms.drv->set_range(&ms, 12, 0), NULL);
	zassert_equal(start_range, ms.current_range, NULL);
	zassert_equal(BMA2x2_RANGE_2G,
		      bma_emul_get_reg(emul, BMA2x2_RANGE_SELECT_ADDR), NULL);
	zassert_equal(-EIO, ms.drv->set_range(&ms, 12, 1), NULL);
	zassert_equal(start_range, ms.current_range, NULL);
	zassert_equal(BMA2x2_RANGE_2G,
		      bma_emul_get_reg(emul, BMA2x2_RANGE_SELECT_ADDR), NULL);

	/* Do not fail on read */
	bma_emul_set_read_fail_reg(emul, BMA_EMUL_NO_FAIL_REG);

	/* Setup emulator fail on write */
	bma_emul_set_write_fail_reg(emul, BMA2x2_RANGE_SELECT_ADDR);

	/* Test fail on write */
	zassert_equal(-EIO, ms.drv->set_range(&ms, 12, 0), NULL);
	zassert_equal(start_range, ms.current_range, NULL);
	zassert_equal(BMA2x2_RANGE_2G,
		      bma_emul_get_reg(emul, BMA2x2_RANGE_SELECT_ADDR), NULL);
	zassert_equal(-EIO, ms.drv->set_range(&ms, 12, 1), NULL);
	zassert_equal(start_range, ms.current_range, NULL);
	zassert_equal(BMA2x2_RANGE_2G,
		      bma_emul_get_reg(emul, BMA2x2_RANGE_SELECT_ADDR), NULL);

	/* Do not fail on write */
	bma_emul_set_write_fail_reg(emul, BMA_EMUL_NO_FAIL_REG);

	/* Test setting range with rounding down */
	check_set_range(emul, 1, 0, 2);
	check_set_range(emul, 2, 0, 2);
	check_set_range(emul, 3, 0, 2);
	check_set_range(emul, 4, 0, 4);
	check_set_range(emul, 5, 0, 4);
	check_set_range(emul, 6, 0, 4);
	check_set_range(emul, 7, 0, 4);
	check_set_range(emul, 8, 0, 8);
	check_set_range(emul, 9, 0, 8);
	check_set_range(emul, 15, 0, 8);
	check_set_range(emul, 16, 0, 16);
	check_set_range(emul, 17, 0, 16);

	/* Test setting range with rounding up */
	check_set_range(emul, 1, 1, 2);
	check_set_range(emul, 2, 1, 2);
	check_set_range(emul, 3, 1, 4);
	check_set_range(emul, 4, 1, 4);
	check_set_range(emul, 5, 1, 8);
	check_set_range(emul, 6, 1, 8);
	check_set_range(emul, 7, 1, 8);
	check_set_range(emul, 8, 1, 8);
	check_set_range(emul, 9, 1, 16);
	check_set_range(emul, 15, 1, 16);
	check_set_range(emul, 16, 1, 16);
	check_set_range(emul, 17, 1, 16);
}

/** Test init with and without I2C errors. */
static void test_bma_init(void)
{
	struct reset_func_data reset_func_data;
	struct i2c_emul *emul;

	emul = bma_emul_get(BMA_ORD);

	/* Setup emulator fail read function */
	bma_emul_set_read_fail_reg(emul, BMA2x2_CHIP_ID_ADDR);

	/* Test fail on chip id read */
	zassert_equal(EC_ERROR_UNKNOWN, ms.drv->init(&ms), NULL);

	/* Disable failing on chip id read, but set wrong value */
	bma_emul_set_read_fail_reg(emul, BMA_EMUL_NO_FAIL_REG);
	bma_emul_set_reg(emul, BMA2x2_CHIP_ID_ADDR, 23);

	/* Test wrong chip id */
	zassert_equal(EC_ERROR_ACCESS_DENIED, ms.drv->init(&ms), NULL);

	/* Set correct chip id, but fail on reset reg read */
	bma_emul_set_read_fail_reg(emul, BMA2x2_RST_ADDR);
	bma_emul_set_reg(emul, BMA2x2_CHIP_ID_ADDR, BMA255_CHIP_ID_MAJOR);

	/* Test fail on reset register read */
	zassert_equal(-EIO, ms.drv->init(&ms), NULL);

	/* Do not fail on read */
	bma_emul_set_read_fail_reg(emul, BMA_EMUL_NO_FAIL_REG);

	/* Setup emulator fail on write */
	bma_emul_set_write_fail_reg(emul, BMA2x2_RST_ADDR);

	/* Test fail on reset register write */
	zassert_equal(-EIO, ms.drv->init(&ms), NULL);

	/* Do not fail on write */
	bma_emul_set_write_fail_reg(emul, BMA_EMUL_NO_FAIL_REG);

	/* Setup emulator fail reset read function */
	reset_func_data.ok_before_fail = 1;
	reset_func_data.fail_attempts = 100;
	reset_func_data.reset_value = 0;
	bma_emul_set_read_func(emul, emul_read_reset, &reset_func_data);

	/* Test fail on too many reset read errors */
	zassert_equal(EC_ERROR_TIMEOUT, ms.drv->init(&ms), NULL);

	/* Test success after reset read errors */
	reset_func_data.ok_before_fail = 1;
	reset_func_data.fail_attempts = 3;
	zassert_equal(EC_RES_SUCCESS, ms.drv->init(&ms), NULL);

	/* Test success without read errors */
	reset_func_data.fail_attempts = 0;
	zassert_equal(EC_RES_SUCCESS, ms.drv->init(&ms), NULL);

	/* Test fail on too many reset read wrong value */
	reset_func_data.fail_attempts = 0;
	reset_func_data.reset_value = 100;
	zassert_equal(EC_ERROR_TIMEOUT, ms.drv->init(&ms), NULL);

	/* Test success on few reset read wrong value */
	reset_func_data.fail_attempts = 0;
	reset_func_data.reset_value = 4;
	zassert_equal(EC_RES_SUCCESS, ms.drv->init(&ms), NULL);

	/* Remove custom emulator read function */
	bma_emul_set_read_func(emul, NULL, NULL);
}

/*
 * Try to set data rate and check if expected rate was set in driver and in
 * emulator.
 */
static void check_set_rate_f(struct i2c_emul *emul, int rate, int rnd,
			     int exp_rate, int line)
{
	uint8_t exp_rate_reg;
	uint8_t rate_reg;
	int drv_rate;

	zassert_equal(EC_SUCCESS, ms.drv->set_data_rate(&ms, rate, rnd),
		      "set_data_rate failed; line: %d", line);
	drv_rate = ms.drv->get_data_rate(&ms);
	zassert_equal(exp_rate, drv_rate, "Expected rate %d, got %d; line %d",
		      exp_rate, drv_rate, line);
	rate_reg = bma_emul_get_reg(emul, BMA2x2_BW_SELECT_ADDR);
	rate_reg &= BMA2x2_BW_MSK;

	switch (exp_rate) {
	case 7812:
		exp_rate_reg = BMA2x2_BW_7_81HZ;
		break;
	case 15625:
		exp_rate_reg = BMA2x2_BW_15_63HZ;
		break;
	case 31250:
		exp_rate_reg = BMA2x2_BW_31_25HZ;
		break;
	case 62500:
		exp_rate_reg = BMA2x2_BW_62_50HZ;
		break;
	case 125000:
		exp_rate_reg = BMA2x2_BW_125HZ;
		break;
	case 250000:
		exp_rate_reg = BMA2x2_BW_250HZ;
		break;
	case 500000:
		exp_rate_reg = BMA2x2_BW_500HZ;
		break;
	case 1000000:
		exp_rate_reg = BMA2x2_BW_1000HZ;
		break;
	default:
		/* Unknown expected rate */
		zassert_unreachable(
			"Expected rate %d not supported by device; line %d",
			exp_rate, line);
		return;
	}

	zassert_equal(exp_rate_reg, rate_reg,
		      "Expected rate reg 0x%x, got 0x%x; line %d",
		      exp_rate_reg, rate_reg, line);
}
#define check_set_rate(emul, rate, rnd, exp_rate)	\
	check_set_rate_f(emul, rate, rnd, exp_rate, __LINE__)

/** Test set and get rate with and without I2C errors. */
static void test_bma_rate(void)
{
	struct i2c_emul *emul;
	uint8_t reg_rate;
	int drv_rate;

	emul = bma_emul_get(BMA_ORD);

	/* Test setting rate with rounding down */
	check_set_rate(emul, 1, 0, 7812);
	check_set_rate(emul, 1, 0, 7812);
	check_set_rate(emul, 7811, 0, 7812);
	check_set_rate(emul, 7812, 0, 7812);
	check_set_rate(emul, 7813, 0, 7812);
	check_set_rate(emul, 15624, 0, 7812);
	check_set_rate(emul, 15625, 0, 15625);
	check_set_rate(emul, 15626, 0, 15625);
	check_set_rate(emul, 31249, 0, 15625);
	check_set_rate(emul, 31250, 0, 31250);
	check_set_rate(emul, 31251, 0, 31250);
	check_set_rate(emul, 62499, 0, 31250);
	check_set_rate(emul, 62500, 0, 62500);
	check_set_rate(emul, 62501, 0, 62500);
	check_set_rate(emul, 124999, 0, 62500);
	check_set_rate(emul, 125000, 0, 125000);
	check_set_rate(emul, 125001, 0, 125000);
	check_set_rate(emul, 249999, 0, 125000);
	check_set_rate(emul, 250000, 0, 250000);
	check_set_rate(emul, 250001, 0, 250000);
	check_set_rate(emul, 499999, 0, 250000);
	check_set_rate(emul, 500000, 0, 500000);
	check_set_rate(emul, 500001, 0, 500000);
	check_set_rate(emul, 999999, 0, 500000);
	check_set_rate(emul, 1000000, 0, 1000000);
	check_set_rate(emul, 1000001, 0, 1000000);
	check_set_rate(emul, 2000000, 0, 1000000);

	/* Test setting rate with rounding up */
	check_set_rate(emul, 1, 1, 7812);
	check_set_rate(emul, 1, 1, 7812);
	check_set_rate(emul, 7811, 1, 7812);
	check_set_rate(emul, 7812, 1, 7812);
	check_set_rate(emul, 7813, 1, 15625);
	check_set_rate(emul, 15624, 1, 15625);
	check_set_rate(emul, 15625, 1, 15625);
	check_set_rate(emul, 15626, 1, 31250);
	check_set_rate(emul, 31249, 1, 31250);
	check_set_rate(emul, 31250, 1, 31250);
	check_set_rate(emul, 31251, 1, 62500);
	check_set_rate(emul, 62499, 1, 62500);
	check_set_rate(emul, 62500, 1, 62500);
	check_set_rate(emul, 62501, 1, 125000);
	check_set_rate(emul, 124999, 1, 125000);
	check_set_rate(emul, 125000, 1, 125000);
	check_set_rate(emul, 125001, 1, 250000);
	check_set_rate(emul, 249999, 1, 250000);
	check_set_rate(emul, 250000, 1, 250000);
	check_set_rate(emul, 250001, 1, 500000);
	check_set_rate(emul, 499999, 1, 500000);
	check_set_rate(emul, 500000, 1, 500000);
	check_set_rate(emul, 500001, 1, 1000000);
	check_set_rate(emul, 999999, 1, 1000000);
	check_set_rate(emul, 1000000, 1, 1000000);
	check_set_rate(emul, 1000001, 1, 1000000);
	check_set_rate(emul, 2000000, 1, 1000000);

	/* Current rate shouldn't be changed on error */
	drv_rate = ms.drv->get_data_rate(&ms);
	reg_rate = bma_emul_get_reg(emul, BMA2x2_BW_SELECT_ADDR);

	/* Setup emulator fail on read */
	bma_emul_set_read_fail_reg(emul, BMA2x2_BW_SELECT_ADDR);

	/* Test fail on read */
	zassert_equal(-EIO, ms.drv->set_data_rate(&ms, 15625, 0), NULL);
	zassert_equal(drv_rate, ms.drv->get_data_rate(&ms), NULL);
	zassert_equal(reg_rate,
		      bma_emul_get_reg(emul, BMA2x2_BW_SELECT_ADDR), NULL);
	zassert_equal(-EIO, ms.drv->set_data_rate(&ms, 15625, 1), NULL);
	zassert_equal(drv_rate, ms.drv->get_data_rate(&ms), NULL);
	zassert_equal(reg_rate,
		      bma_emul_get_reg(emul, BMA2x2_BW_SELECT_ADDR), NULL);

	/* Do not fail on read */
	bma_emul_set_read_fail_reg(emul, BMA_EMUL_NO_FAIL_REG);

	/* Setup emulator fail on write */
	bma_emul_set_write_fail_reg(emul, BMA2x2_BW_SELECT_ADDR);

	/* Test fail on write */
	zassert_equal(-EIO, ms.drv->set_data_rate(&ms, 15625, 0), NULL);
	zassert_equal(drv_rate, ms.drv->get_data_rate(&ms), NULL);
	zassert_equal(reg_rate,
		      bma_emul_get_reg(emul, BMA2x2_BW_SELECT_ADDR), NULL);
	zassert_equal(-EIO, ms.drv->set_data_rate(&ms, 15625, 1), NULL);
	zassert_equal(drv_rate, ms.drv->get_data_rate(&ms), NULL);
	zassert_equal(reg_rate,
		      bma_emul_get_reg(emul, BMA2x2_BW_SELECT_ADDR), NULL);

	/* Do not fail on write */
	bma_emul_set_write_fail_reg(emul, BMA_EMUL_NO_FAIL_REG);
}

/** Test read with and without I2C errors. */
static void test_bma_read(void)
{
	struct i2c_emul *emul;
	int16_t ret_acc[3];
	int16_t exp_acc[3];
	intv3_t ret_acc_v;
	uint8_t reg_rate;
	int drv_rate;

	emul = bma_emul_get(BMA_ORD);

	/* Set offset 0 to simplify test */
	bma_emul_set_off(emul, BMA_EMUL_AXIS_X, 0);
	bma_emul_set_off(emul, BMA_EMUL_AXIS_Y, 0);
	bma_emul_set_off(emul, BMA_EMUL_AXIS_Z, 0);

	/* Test fail on each axis */
	bma_emul_set_read_fail_reg(emul, BMA2x2_X_AXIS_LSB_ADDR);
	zassert_equal(-EIO, ms.drv->read(&ms, ret_acc_v), NULL);
	bma_emul_set_read_fail_reg(emul, BMA2x2_X_AXIS_MSB_ADDR);
	zassert_equal(-EIO, ms.drv->read(&ms, ret_acc_v), NULL);
	bma_emul_set_read_fail_reg(emul, BMA2x2_Y_AXIS_LSB_ADDR);
	zassert_equal(-EIO, ms.drv->read(&ms, ret_acc_v), NULL);
	bma_emul_set_read_fail_reg(emul, BMA2x2_Y_AXIS_MSB_ADDR);
	zassert_equal(-EIO, ms.drv->read(&ms, ret_acc_v), NULL);
	bma_emul_set_read_fail_reg(emul, BMA2x2_Z_AXIS_LSB_ADDR);
	zassert_equal(-EIO, ms.drv->read(&ms, ret_acc_v), NULL);
	bma_emul_set_read_fail_reg(emul, BMA2x2_Z_AXIS_MSB_ADDR);
	zassert_equal(-EIO, ms.drv->read(&ms, ret_acc_v), NULL);

	/* Do not fail on read */
	bma_emul_set_read_fail_reg(emul, BMA_EMUL_NO_FAIL_REG);

	/* Set input accelerometer values */
	exp_acc[0] = BMA_EMUL_1G / 10;
	exp_acc[1] = BMA_EMUL_1G / 20;
	exp_acc[2] = -(int)BMA_EMUL_1G / 30;
	set_emul_acc(emul, exp_acc);
	/* Disable rotation */
	ms.rot_standard_ref = NULL;
	/* Set range to 2G */
	zassert_equal(EC_SUCCESS, ms.drv->set_range(&ms, 2, 0), NULL);

	/* Test read without rotation */
	zassert_equal(EC_SUCCESS, ms.drv->read(&ms, ret_acc_v), NULL);
	drv_acc_to_emul(ret_acc_v, 2, ret_acc);
	compare_int3v(exp_acc, ret_acc);

	/* Set range to 4G */
	zassert_equal(EC_SUCCESS, ms.drv->set_range(&ms, 4, 0), NULL);

	/* Test read without rotation */
	zassert_equal(EC_SUCCESS, ms.drv->read(&ms, ret_acc_v), NULL);
	drv_acc_to_emul(ret_acc_v, 4, ret_acc);
	compare_int3v(exp_acc, ret_acc);

	/* Setup rotation and rotate expected vector */
	ms.rot_standard_ref = &test_rotation;
	rotate_int3v_by_test_rotation(exp_acc);
	/* Set range to 2G */
	zassert_equal(EC_SUCCESS, ms.drv->set_range(&ms, 2, 0), NULL);

	/* Test read with rotation */
	zassert_equal(EC_SUCCESS, ms.drv->read(&ms, ret_acc_v), NULL);
	drv_acc_to_emul(ret_acc_v, 2, ret_acc);
	compare_int3v(exp_acc, ret_acc);

	/* Set range to 4G */
	zassert_equal(EC_SUCCESS, ms.drv->set_range(&ms, 4, 0), NULL);

	/* Test read with rotation */
	zassert_equal(EC_SUCCESS, ms.drv->read(&ms, ret_acc_v), NULL);
	drv_acc_to_emul(ret_acc_v, 4, ret_acc);
	compare_int3v(exp_acc, ret_acc);
}

/** Data for functions used in perform_calib test */
struct calib_func_data {
	/** Time when offset compensation where triggered */
	int calib_start;
	/** Time how long offset cal ready should be unset */
	int time;
	/** Flag indicate if read should fail after compensation is triggered */
	int read_fail;
};

/**
 * Custom emulator read function used in perform_calib test. It controls if
 * cal ready bit in offset control register should be set. It is set after
 * data.time miliseconds passed from data.calib_start time. Function returns
 * error when offset control register is accessed when cal ready bit is not set
 * and data.read_fail is not zero.
 */
static int emul_read_calib_func(struct i2c_emul *emul, int reg, void *data)
{
	struct calib_func_data *d = data;
	uint8_t reg_val;
	int cur_time;

	if (reg != BMA2x2_OFFSET_CTRL_ADDR) {
		return 1;
	}

	reg_val = bma_emul_get_reg(emul, BMA2x2_OFFSET_CTRL_ADDR);
	cur_time = k_uptime_get_32();
	if (cur_time - d->calib_start < d->time) {
		if (d->read_fail) {
			return -EIO;
		}
		reg_val &= ~BMA2x2_OFFSET_CAL_READY;
	} else {
		reg_val |= BMA2x2_OFFSET_CAL_READY;
	}
	bma_emul_set_reg(emul, BMA2x2_OFFSET_CTRL_ADDR, reg_val);

	return 1;
}

/**
 * Custom emulator write function used in perform_calib test. It sets
 * calib_start field in data with time when offset compensation process was
 * triggerd.
 */
static int emul_write_calib_func(struct i2c_emul *emul, int reg, uint8_t val,
				 void *data)
{
	struct calib_func_data *d = data;
	int cur_time;

	if (reg != BMA2x2_OFFSET_CTRL_ADDR) {
		return 1;
	}

	if (val & BMA2x2_OFFSET_TRIGGER_MASK) {
		d->calib_start = k_uptime_get_32();
	}

	return 1;
}

/** Test offset compensation with and without I2C errors. */
static void test_bma_perform_calib(void)
{
	struct calib_func_data func_data;
	struct i2c_emul *emul;
	int16_t start_off[3];
	int16_t exp_off[3];
	int16_t ret_off[3];
	int range;
	int rate;
	mat33_fp_t rot = {
		{ FLOAT_TO_FP(1), 0, 0},
		{ 0, FLOAT_TO_FP(1), 0},
		{ 0, 0, FLOAT_TO_FP(-1)}
	};

	emul = bma_emul_get(BMA_ORD);

	/* Range and rate cannot change after calibration */
	range = 4;
	rate = 125000;
	zassert_equal(EC_SUCCESS, ms.drv->set_range(&ms, range, 0), NULL);
	zassert_equal(EC_SUCCESS, ms.drv->set_data_rate(&ms, rate, 0), NULL);

	/* Set offset 0 */
	start_off[0] = 0;
	start_off[1] = 0;
	start_off[2] = 0;
	set_emul_offset(emul, start_off);

	/* Set input accelerometer values */
	exp_off[0] = BMA_EMUL_1G / 10;
	exp_off[1] = BMA_EMUL_1G / 20;
	exp_off[2] = -(int)BMA_EMUL_1G / 30;
	set_emul_acc(emul, exp_off);

	/*
	 * Expected offset is [-X, -Y, 1G - Z] for no rotation or positive
	 * rotation on Z axis
	 */
	exp_off[0] = -exp_off[0];
	exp_off[1] = -exp_off[1];
	exp_off[2] = BMA_EMUL_1G - exp_off[2];

	/* Setup emulator calibration functions */
	bma_emul_set_read_func(emul, emul_read_calib_func, &func_data);
	bma_emul_set_write_func(emul, emul_write_calib_func, &func_data);

	/* Setup emulator to fail on first access to offset control register */
	func_data.calib_start = k_uptime_get_32();
	func_data.read_fail = 1;
	func_data.time = 1000000;

	/* Test success on disabling calibration */
	zassert_equal(EC_SUCCESS, ms.drv->perform_calib(&ms, 0), NULL);
	zassert_equal(range, ms.current_range, NULL);
	zassert_equal(rate, ms.drv->get_data_rate(&ms), NULL);

	/* Test fail on first access to offset control register */
	zassert_equal(-EIO, ms.drv->perform_calib(&ms, 1), NULL);
	zassert_equal(range, ms.current_range, NULL);
	zassert_equal(rate, ms.drv->get_data_rate(&ms), NULL);

	/* Setup emulator to return cal not ready */
	func_data.calib_start = k_uptime_get_32();
	func_data.read_fail = 0;
	func_data.time = 1000000;

	/* Test fail on cal not ready */
	zassert_equal(EC_ERROR_ACCESS_DENIED, ms.drv->perform_calib(&ms, 1),
		      NULL);
	zassert_equal(range, ms.current_range, NULL);
	zassert_equal(rate, ms.drv->get_data_rate(&ms), NULL);

	/*
	 * Setup emulator to fail on access to offset control register after
	 * triggering offset compensation
	 */
	func_data.calib_start = 0;
	func_data.read_fail = 1;
	func_data.time = 160;

	/* Test fail on read during offset compensation */
	zassert_equal(-EIO, ms.drv->perform_calib(&ms, 1), NULL);
	zassert_equal(range, ms.current_range, NULL);
	zassert_equal(rate, ms.drv->get_data_rate(&ms), NULL);

	/*
	 * Setup emulator to return cal not ready for 1s after triggering
	 * offset compensation
	 */
	func_data.calib_start = 0;
	func_data.read_fail = 0;
	func_data.time = 1000;

	/* Test fail on too long offset compensation */
	zassert_equal(EC_RES_TIMEOUT, ms.drv->perform_calib(&ms, 1), NULL);
	zassert_equal(range, ms.current_range, NULL);
	zassert_equal(rate, ms.drv->get_data_rate(&ms), NULL);

	/*
	 * Setup emulator to return cal not ready for 160ms after triggering
	 * offset compensation
	 */
	func_data.calib_start = 0;
	func_data.read_fail = 0;
	func_data.time = 160;
	/* Disable rotation */
	ms.rot_standard_ref = NULL;

	/* Test successful offset compenastion without rotation */
	zassert_equal(EC_SUCCESS, ms.drv->perform_calib(&ms, 1), NULL);
	zassert_equal(range, ms.current_range, NULL);
	zassert_equal(rate, ms.drv->get_data_rate(&ms), NULL);
	get_emul_offset(emul, ret_off);
	compare_int3v(exp_off, ret_off);

	func_data.calib_start = 0;
	/* Enable rotation with negative value on Z axis */
	ms.rot_standard_ref = &rot;
	/* Expected offset -1G - accelerometer[Z] */
	exp_off[2] = -((int)BMA_EMUL_1G) - bma_emul_get_acc(emul,
							    BMA_EMUL_AXIS_Z);

	/* Test successful offset compenastion with negative Z rotation */
	zassert_equal(EC_SUCCESS, ms.drv->perform_calib(&ms, 1), NULL);
	zassert_equal(range, ms.current_range, NULL);
	zassert_equal(rate, ms.drv->get_data_rate(&ms), NULL);
	get_emul_offset(emul, ret_off);
	compare_int3v(exp_off, ret_off);

	func_data.calib_start = 0;
	/* Set positive rotation on Z axis */
	rot[2][2] = FLOAT_TO_FP(1);
	/* Expected offset 1G - accelerometer[Z] */
	exp_off[2] = BMA_EMUL_1G - bma_emul_get_acc(emul, BMA_EMUL_AXIS_Z);

	/* Test successful offset compenastion with positive Z rotation */
	zassert_equal(EC_SUCCESS, ms.drv->perform_calib(&ms, 1), NULL);
	zassert_equal(range, ms.current_range, NULL);
	zassert_equal(rate, ms.drv->get_data_rate(&ms), NULL);
	get_emul_offset(emul, ret_off);
	compare_int3v(exp_off, ret_off);

	/* Remove custom emulator functions */
	bma_emul_set_read_func(emul, NULL, NULL);
	bma_emul_set_write_func(emul, NULL, NULL);
}

/** Test get resolution. */
static void test_bma_get_resolution(void)
{
	/* Resolution should be always 12 bits */
	zassert_equal(12, ms.drv->get_resolution(&ms), NULL);
}

void test_suite_bma2x2(void)
{
	k_mutex_init(&sensor_mutex);

	ztest_test_suite(bma2x2,
			 ztest_user_unit_test(test_bma_get_offset),
			 ztest_user_unit_test(test_bma_set_offset),
			 ztest_user_unit_test(test_bma_set_range),
			 ztest_user_unit_test(test_bma_init),
			 ztest_user_unit_test(test_bma_rate),
			 ztest_user_unit_test(test_bma_read),
			 ztest_user_unit_test(test_bma_perform_calib),
			 ztest_user_unit_test(test_bma_get_resolution));
	ztest_run_test_suite(bma2x2);
}
