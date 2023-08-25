/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/accelgyro_bmi_common.h"
#include "emul/emul_bmi.h"
#include "emul/emul_common_i2c.h"
#include "i2c.h"
#include "motion_sense_fifo.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define BMI_NODE DT_NODELABEL(accel_bmi160)
#define BMI_ACC_SENSOR_ID SENSOR_ID(DT_NODELABEL(ms_bmi160_accel))
#define BMI_GYR_SENSOR_ID SENSOR_ID(DT_NODELABEL(ms_bmi160_gyro))
#define BMI_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(SENSOR_ID(DT_ALIAS(bmi160_int)))

/** How accurate comparision of vectors should be */
#define V_EPS 8

/** Convert from one type of vector to another */
#define convert_int3v_int16(v, r) \
	do {                      \
		r[0] = v[0];      \
		r[1] = v[1];      \
		r[2] = v[2];      \
	} while (0)

/** Rotation used in some tests */
static const mat33_fp_t test_rotation = { { 0, FLOAT_TO_FP(1), 0 },
					  { FLOAT_TO_FP(-1), 0, 0 },
					  { 0, 0, FLOAT_TO_FP(-1) } };
/** Rotate given vector by test rotation */
static void rotate_int3v_by_test_rotation(intv3_t v)
{
	int16_t t;

	t = v[0];
	v[0] = -v[1];
	v[1] = t;
	v[2] = -v[2];
}

/** Set emulator accelerometer offset values to intv3_t vector */
static void set_emul_acc_offset(const struct emul *emul, intv3_t offset)
{
	bmi_emul_set_off(emul, BMI_EMUL_ACC_X, offset[0]);
	bmi_emul_set_off(emul, BMI_EMUL_ACC_Y, offset[1]);
	bmi_emul_set_off(emul, BMI_EMUL_ACC_Z, offset[2]);
}

/** Save emulator accelerometer offset values to intv3_t vector */
static void get_emul_acc_offset(const struct emul *emul, intv3_t offset)
{
	offset[0] = bmi_emul_get_off(emul, BMI_EMUL_ACC_X);
	offset[1] = bmi_emul_get_off(emul, BMI_EMUL_ACC_Y);
	offset[2] = bmi_emul_get_off(emul, BMI_EMUL_ACC_Z);
}

/** Set emulator accelerometer values to intv3_t vector */
static void set_emul_acc(const struct emul *emul, intv3_t acc)
{
	bmi_emul_set_value(emul, BMI_EMUL_ACC_X, acc[0]);
	bmi_emul_set_value(emul, BMI_EMUL_ACC_Y, acc[1]);
	bmi_emul_set_value(emul, BMI_EMUL_ACC_Z, acc[2]);
}

/** Set emulator gyroscope offset values to intv3_t vector */
static void set_emul_gyr_offset(const struct emul *emul, intv3_t offset)
{
	bmi_emul_set_off(emul, BMI_EMUL_GYR_X, offset[0]);
	bmi_emul_set_off(emul, BMI_EMUL_GYR_Y, offset[1]);
	bmi_emul_set_off(emul, BMI_EMUL_GYR_Z, offset[2]);
}

/** Save emulator gyroscope offset values to intv3_t vector */
static void get_emul_gyr_offset(const struct emul *emul, intv3_t offset)
{
	offset[0] = bmi_emul_get_off(emul, BMI_EMUL_GYR_X);
	offset[1] = bmi_emul_get_off(emul, BMI_EMUL_GYR_Y);
	offset[2] = bmi_emul_get_off(emul, BMI_EMUL_GYR_Z);
}

/** Set emulator gyroscope values to vector of three int16_t */
static void set_emul_gyr(const struct emul *emul, intv3_t gyr)
{
	bmi_emul_set_value(emul, BMI_EMUL_GYR_X, gyr[0]);
	bmi_emul_set_value(emul, BMI_EMUL_GYR_Y, gyr[1]);
	bmi_emul_set_value(emul, BMI_EMUL_GYR_Z, gyr[2]);
}

/** Convert accelerometer read to units used by emulator */
static void drv_acc_to_emul(intv3_t drv, int range, intv3_t out)
{
	const int scale = MOTION_SCALING_FACTOR / BMI_EMUL_1G;

	out[0] = drv[0] * range / scale;
	out[1] = drv[1] * range / scale;
	out[2] = drv[2] * range / scale;
}

/** Convert gyroscope read to units used by emulator */
static void drv_gyr_to_emul(intv3_t drv, int range, intv3_t out)
{
	const int scale = MOTION_SCALING_FACTOR / BMI_EMUL_125_DEG_S;

	range /= 125;
	out[0] = drv[0] * range / scale;
	out[1] = drv[1] * range / scale;
	out[2] = drv[2] * range / scale;
}

/** Compare two vectors of intv3_t type */
static void compare_int3v_f(intv3_t exp_v, intv3_t v, int eps, int line)
{
	int i;

	for (i = 0; i < 3; i++) {
		zassert_within(
			exp_v[i], v[i], eps,
			"Expected [%d; %d; %d], got [%d; %d; %d]; line: %d",
			exp_v[0], exp_v[1], exp_v[2], v[0], v[1], v[2], line);
	}
}
#define compare_int3v_eps(exp_v, v, e) compare_int3v_f(exp_v, v, e, __LINE__)
#define compare_int3v(exp_v, v) compare_int3v_eps(exp_v, v, V_EPS)

/** Test get accelerometer offset with and without rotation */
ZTEST_USER(bmi160, test_bmi_acc_get_offset)
{
	struct motion_sensor_t *ms;
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	struct i2c_common_emul_data *common_data =
		emul_bmi_get_i2c_common_data(emul);
	int16_t ret[3];
	intv3_t ret_v;
	intv3_t exp_v;
	int16_t temp;

	ms = &motion_sensors[BMI_ACC_SENSOR_ID];

	/* Set emulator offset */
	exp_v[0] = BMI_EMUL_1G / 10;
	exp_v[1] = BMI_EMUL_1G / 20;
	exp_v[2] = -(int)BMI_EMUL_1G / 30;
	set_emul_acc_offset(emul, exp_v);
	/* BMI driver returns value in mg units */
	exp_v[0] = 1000 / 10;
	exp_v[1] = 1000 / 20;
	exp_v[2] = -1000 / 30;

	/* Test fail on offset read */
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_OFFSET_ACC70);
	zassert_equal(EC_ERROR_INVAL, ms->drv->get_offset(ms, ret, &temp),
		      NULL);
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_OFFSET_ACC70 + 1);
	zassert_equal(EC_ERROR_INVAL, ms->drv->get_offset(ms, ret, &temp),
		      NULL);
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_OFFSET_ACC70 + 2);
	zassert_equal(EC_ERROR_INVAL, ms->drv->get_offset(ms, ret, &temp),
		      NULL);
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Disable rotation */
	ms->rot_standard_ref = NULL;

	/* Test get offset without rotation */
	zassert_equal(EC_SUCCESS, ms->drv->get_offset(ms, ret, &temp));
	zassert_equal(temp, (int16_t)EC_MOTION_SENSE_INVALID_CALIB_TEMP);
	convert_int3v_int16(ret, ret_v);
	compare_int3v(exp_v, ret_v);

	/* Setup rotation and rotate expected offset */
	ms->rot_standard_ref = &test_rotation;
	rotate_int3v_by_test_rotation(exp_v);

	/* Test get offset with rotation */
	zassert_equal(EC_SUCCESS, ms->drv->get_offset(ms, ret, &temp));
	zassert_equal(temp, (int16_t)EC_MOTION_SENSE_INVALID_CALIB_TEMP);
	convert_int3v_int16(ret, ret_v);
	compare_int3v(exp_v, ret_v);
}

/** Test get gyroscope offset with and without rotation */
ZTEST_USER(bmi160, test_bmi_gyr_get_offset)
{
	struct motion_sensor_t *ms;
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	struct i2c_common_emul_data *common_data =
		emul_bmi_get_i2c_common_data(emul);
	int16_t ret[3];
	intv3_t ret_v;
	intv3_t exp_v;
	int16_t temp;

	ms = &motion_sensors[BMI_GYR_SENSOR_ID];

	/* Do not fail on read */
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Set emulator offset */
	exp_v[0] = BMI_EMUL_125_DEG_S / 100;
	exp_v[1] = BMI_EMUL_125_DEG_S / 200;
	exp_v[2] = -(int)BMI_EMUL_125_DEG_S / 300;
	set_emul_gyr_offset(emul, exp_v);
	/* BMI driver returns value in mdeg/s units */
	exp_v[0] = 125000 / 100;
	exp_v[1] = 125000 / 200;
	exp_v[2] = -125000 / 300;

	/* Test fail on offset read */
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_OFFSET_GYR70);
	zassert_equal(EC_ERROR_INVAL, ms->drv->get_offset(ms, ret, &temp),
		      NULL);
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_OFFSET_GYR70 + 1);
	zassert_equal(EC_ERROR_INVAL, ms->drv->get_offset(ms, ret, &temp),
		      NULL);
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_OFFSET_GYR70 + 2);
	zassert_equal(EC_ERROR_INVAL, ms->drv->get_offset(ms, ret, &temp),
		      NULL);
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_OFFSET_EN_GYR98);
	zassert_equal(EC_ERROR_INVAL, ms->drv->get_offset(ms, ret, &temp),
		      NULL);
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Disable rotation */
	ms->rot_standard_ref = NULL;

	/* Test get offset without rotation */
	zassert_equal(EC_SUCCESS, ms->drv->get_offset(ms, ret, &temp));
	zassert_equal(temp, (int16_t)EC_MOTION_SENSE_INVALID_CALIB_TEMP);
	convert_int3v_int16(ret, ret_v);
	compare_int3v_eps(exp_v, ret_v, 64);

	/* Setup rotation and rotate expected offset */
	ms->rot_standard_ref = &test_rotation;
	rotate_int3v_by_test_rotation(exp_v);

	/* Test get offset with rotation */
	zassert_equal(EC_SUCCESS, ms->drv->get_offset(ms, ret, &temp));
	zassert_equal(temp, (int16_t)EC_MOTION_SENSE_INVALID_CALIB_TEMP);
	convert_int3v_int16(ret, ret_v);
	compare_int3v_eps(exp_v, ret_v, 64);
}

/**
 * Test set accelerometer offset with and without rotation. Also test behaviour
 * on I2C error.
 */
ZTEST_USER(bmi160, test_bmi_acc_set_offset)
{
	struct motion_sensor_t *ms;
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	struct i2c_common_emul_data *common_data =
		emul_bmi_get_i2c_common_data(emul);
	int16_t input_v[3];
	int16_t temp = 0;
	intv3_t ret_v;
	intv3_t exp_v;

	ms = &motion_sensors[BMI_ACC_SENSOR_ID];

	/* Test fail on OFFSET EN GYR98 register read and write */
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_OFFSET_EN_GYR98);
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_offset(ms, input_v, temp),
		      NULL);
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(common_data, BMI160_OFFSET_EN_GYR98);
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_offset(ms, input_v, temp),
		      NULL);
	i2c_common_emul_set_write_fail_reg(common_data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test fail on offset write */
	i2c_common_emul_set_write_fail_reg(common_data, BMI160_OFFSET_ACC70);
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_offset(ms, input_v, temp),
		      NULL);
	i2c_common_emul_set_write_fail_reg(common_data,
					   BMI160_OFFSET_ACC70 + 1);
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_offset(ms, input_v, temp),
		      NULL);
	i2c_common_emul_set_write_fail_reg(common_data,
					   BMI160_OFFSET_ACC70 + 2);
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_offset(ms, input_v, temp),
		      NULL);
	i2c_common_emul_set_write_fail_reg(common_data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Set input offset */
	exp_v[0] = BMI_EMUL_1G / 10;
	exp_v[1] = BMI_EMUL_1G / 20;
	exp_v[2] = -(int)BMI_EMUL_1G / 30;
	/* BMI driver accept value in mg units */
	input_v[0] = 1000 / 10;
	input_v[1] = 1000 / 20;
	input_v[2] = -1000 / 30;
	/* Disable rotation */
	ms->rot_standard_ref = NULL;

	/* Test set offset without rotation */
	zassert_equal(EC_SUCCESS, ms->drv->set_offset(ms, input_v, temp));
	get_emul_acc_offset(emul, ret_v);
	/*
	 * Depending on used range, accelerometer values may be up to 6 bits
	 * more accurate then offset value resolution.
	 */
	compare_int3v_eps(exp_v, ret_v, 64);
	/* Accelerometer offset should be enabled */
	zassert_true(bmi_emul_get_reg(emul, BMI160_OFFSET_EN_GYR98) &
			     BMI160_OFFSET_ACC_EN,
		     NULL);

	/* Setup rotation and rotate input for set_offset function */
	ms->rot_standard_ref = &test_rotation;
	convert_int3v_int16(input_v, ret_v);
	rotate_int3v_by_test_rotation(ret_v);
	convert_int3v_int16(ret_v, input_v);

	/* Test set offset with rotation */
	zassert_equal(EC_SUCCESS, ms->drv->set_offset(ms, input_v, temp));
	get_emul_acc_offset(emul, ret_v);
	compare_int3v_eps(exp_v, ret_v, 64);
	/* Accelerometer offset should be enabled */
	zassert_true(bmi_emul_get_reg(emul, BMI160_OFFSET_EN_GYR98) &
			     BMI160_OFFSET_ACC_EN,
		     NULL);
}

/**
 * Test set accelerometer offset with extreme values.
 */
ZTEST_USER(bmi160, test_bmi_acc_set_offset_min_max)
{
	struct motion_sensor_t *ms;
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	int16_t input_v[3];
	int16_t temp = 0;
	intv3_t ret_v;
	intv3_t exp_v;

	ms = &motion_sensors[BMI_ACC_SENSOR_ID];

	/* Set expected offsets */
	exp_v[0] = 8128;
	exp_v[1] = -8192;
	exp_v[2] = 0;
	/* Set some extreme values. */
	input_v[0] = INT16_MAX;
	input_v[1] = INT16_MIN;
	input_v[2] = 0;
	/* Disable rotation */
	ms->rot_standard_ref = NULL;

	/* Test set offset without rotation */
	zassert_equal(EC_SUCCESS, ms->drv->set_offset(ms, input_v, temp));
	get_emul_acc_offset(emul, ret_v);
	compare_int3v(exp_v, ret_v);
	/* Accelerometer offset should be enabled */
	zassert_true(bmi_emul_get_reg(emul, BMI160_OFFSET_ACC70) &
			     BMI160_OFFSET_ACC_EN,
		     NULL);

	/* Setup rotation and rotate input for set_offset function */
	ms->rot_standard_ref = &test_rotation;
	convert_int3v_int16(input_v, ret_v);
	rotate_int3v_by_test_rotation(ret_v);
	convert_int3v_int16(ret_v, input_v);

	/* Test set offset with rotation */
	zassert_equal(EC_SUCCESS, ms->drv->set_offset(ms, input_v, temp));
	get_emul_acc_offset(emul, ret_v);
	compare_int3v(exp_v, ret_v);
	zassert_true(bmi_emul_get_reg(emul, BMI160_OFFSET_ACC70) &
			     BMI160_OFFSET_ACC_EN,
		     NULL);
}

/**
 * Test set gyroscope offset with and without rotation. Also test behaviour
 * on I2C error.
 */
ZTEST_USER(bmi160, test_bmi_gyr_set_offset)
{
	struct motion_sensor_t *ms;
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	struct i2c_common_emul_data *common_data =
		emul_bmi_get_i2c_common_data(emul);
	int16_t input_v[3] = { 0, 0, 0 };
	int16_t temp = 0;
	intv3_t ret_v;
	intv3_t exp_v;

	ms = &motion_sensors[BMI_GYR_SENSOR_ID];

	/* Test fail on OFFSET EN GYR98 register read and write */
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_OFFSET_EN_GYR98);
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_offset(ms, input_v, temp),
		      NULL);
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(common_data, BMI160_OFFSET_EN_GYR98);
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_offset(ms, input_v, temp),
		      NULL);
	i2c_common_emul_set_write_fail_reg(common_data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test fail on offset write */
	i2c_common_emul_set_write_fail_reg(common_data, BMI160_OFFSET_GYR70);
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_offset(ms, input_v, temp),
		      NULL);
	i2c_common_emul_set_write_fail_reg(common_data,
					   BMI160_OFFSET_GYR70 + 1);
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_offset(ms, input_v, temp),
		      NULL);
	i2c_common_emul_set_write_fail_reg(common_data,
					   BMI160_OFFSET_GYR70 + 2);
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_offset(ms, input_v, temp),
		      NULL);
	i2c_common_emul_set_write_fail_reg(common_data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Set input offset */
	exp_v[0] = BMI_EMUL_125_DEG_S / 100;
	exp_v[1] = BMI_EMUL_125_DEG_S / 200;
	exp_v[2] = -(int)BMI_EMUL_125_DEG_S / 300;
	/* BMI driver accept value in mdeg/s units */
	input_v[0] = 125000 / 100;
	input_v[1] = 125000 / 200;
	input_v[2] = -125000 / 300;
	/* Disable rotation */
	ms->rot_standard_ref = NULL;

	/* Test set offset without rotation */
	zassert_equal(EC_SUCCESS, ms->drv->set_offset(ms, input_v, temp));
	get_emul_gyr_offset(emul, ret_v);
	compare_int3v(exp_v, ret_v);
	/* Gyroscope offset should be enabled */
	zassert_true(bmi_emul_get_reg(emul, BMI160_OFFSET_EN_GYR98) &
			     BMI160_OFFSET_GYRO_EN,
		     NULL);

	/* Setup rotation and rotate input for set_offset function */
	ms->rot_standard_ref = &test_rotation;
	convert_int3v_int16(input_v, ret_v);
	rotate_int3v_by_test_rotation(ret_v);
	convert_int3v_int16(ret_v, input_v);

	/* Test set offset with rotation */
	zassert_equal(EC_SUCCESS, ms->drv->set_offset(ms, input_v, temp));
	get_emul_gyr_offset(emul, ret_v);
	compare_int3v(exp_v, ret_v);
	zassert_true(bmi_emul_get_reg(emul, BMI160_OFFSET_EN_GYR98) &
			     BMI160_OFFSET_GYRO_EN,
		     NULL);
}

/**
 * Test set gyroscope offset with extreme values.
 */
ZTEST_USER(bmi160, test_bmi_gyr_set_offset_min_max)
{
	struct motion_sensor_t *ms;
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	int16_t input_v[3];
	int16_t temp = 0;
	intv3_t ret_v;
	intv3_t exp_v;

	ms = &motion_sensors[BMI_GYR_SENSOR_ID];

	/* Set expected offsets */
	exp_v[0] = 8176;
	exp_v[1] = -8192;
	exp_v[2] = 0;
	/* Set some extreme values. */
	input_v[0] = INT16_MAX;
	input_v[1] = INT16_MIN;
	input_v[2] = 0;
	/* Disable rotation */
	ms->rot_standard_ref = NULL;

	/* Test set offset without rotation */
	zassert_equal(EC_SUCCESS, ms->drv->set_offset(ms, input_v, temp));
	get_emul_gyr_offset(emul, ret_v);
	compare_int3v(exp_v, ret_v);
	/* Gyroscope offset should be enabled */
	zassert_true(bmi_emul_get_reg(emul, BMI160_OFFSET_EN_GYR98) &
			     BMI160_OFFSET_GYRO_EN,
		     NULL);

	/* Setup rotation and rotate input for set_offset function */
	ms->rot_standard_ref = &test_rotation;
	convert_int3v_int16(input_v, ret_v);
	rotate_int3v_by_test_rotation(ret_v);
	convert_int3v_int16(ret_v, input_v);

	/* Test set offset with rotation */
	zassert_equal(EC_SUCCESS, ms->drv->set_offset(ms, input_v, temp));
	get_emul_gyr_offset(emul, ret_v);
	compare_int3v(exp_v, ret_v);
	zassert_true(bmi_emul_get_reg(emul, BMI160_OFFSET_EN_GYR98) &
			     BMI160_OFFSET_GYRO_EN,
		     NULL);
}

/**
 * Try to set accelerometer range and check if expected range was set
 * in driver and in emulator.
 */
static void check_set_acc_range_f(const struct emul *emul,
				  struct motion_sensor_t *ms, int range,
				  int rnd, int exp_range, int line)
{
	uint8_t exp_range_reg;
	uint8_t range_reg;

	zassert_equal(EC_SUCCESS, ms->drv->set_range(ms, range, rnd),
		      "set_range failed; line: %d", line);
	zassert_equal(exp_range, ms->current_range,
		      "Expected range %d, got %d; line %d", exp_range,
		      ms->current_range, line);
	range_reg = bmi_emul_get_reg(emul, BMI160_ACC_RANGE);

	switch (exp_range) {
	case 2:
		exp_range_reg = BMI160_GSEL_2G;
		break;
	case 4:
		exp_range_reg = BMI160_GSEL_4G;
		break;
	case 8:
		exp_range_reg = BMI160_GSEL_8G;
		break;
	case 16:
		exp_range_reg = BMI160_GSEL_16G;
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
#define check_set_acc_range(emul, ms, range, rnd, exp_range) \
	check_set_acc_range_f(emul, ms, range, rnd, exp_range, __LINE__)

/** Test set accelerometer range with and without I2C errors */
ZTEST_USER(bmi160, test_bmi_acc_set_range)
{
	struct motion_sensor_t *ms;
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	struct i2c_common_emul_data *common_data =
		emul_bmi_get_i2c_common_data(emul);
	int start_range;

	ms = &motion_sensors[BMI_ACC_SENSOR_ID];

	/* Setup starting range, shouldn't be changed on error */
	start_range = 2;
	ms->current_range = start_range;
	bmi_emul_set_reg(emul, BMI160_ACC_RANGE, BMI160_GSEL_2G);
	/* Setup emulator fail on write */
	i2c_common_emul_set_write_fail_reg(common_data, BMI160_ACC_RANGE);

	/* Test fail on write */
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_range(ms, 12, 0));
	zassert_equal(start_range, ms->current_range);
	zassert_equal(BMI160_GSEL_2G, bmi_emul_get_reg(emul, BMI160_ACC_RANGE),
		      NULL);
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_range(ms, 12, 1));
	zassert_equal(start_range, ms->current_range);
	zassert_equal(BMI160_GSEL_2G, bmi_emul_get_reg(emul, BMI160_ACC_RANGE),
		      NULL);

	/* Do not fail on write */
	i2c_common_emul_set_write_fail_reg(common_data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test setting range with rounding down */
	check_set_acc_range(emul, ms, 1, 0, 2);
	check_set_acc_range(emul, ms, 2, 0, 2);
	check_set_acc_range(emul, ms, 3, 0, 2);
	check_set_acc_range(emul, ms, 4, 0, 4);
	check_set_acc_range(emul, ms, 5, 0, 4);
	check_set_acc_range(emul, ms, 6, 0, 4);
	check_set_acc_range(emul, ms, 7, 0, 4);
	check_set_acc_range(emul, ms, 8, 0, 8);
	check_set_acc_range(emul, ms, 9, 0, 8);
	check_set_acc_range(emul, ms, 15, 0, 8);
	check_set_acc_range(emul, ms, 16, 0, 16);
	check_set_acc_range(emul, ms, 17, 0, 16);

	/* Test setting range with rounding up */
	check_set_acc_range(emul, ms, 1, 1, 2);
	check_set_acc_range(emul, ms, 2, 1, 2);
	check_set_acc_range(emul, ms, 3, 1, 4);
	check_set_acc_range(emul, ms, 4, 1, 4);
	check_set_acc_range(emul, ms, 5, 1, 8);
	check_set_acc_range(emul, ms, 6, 1, 8);
	check_set_acc_range(emul, ms, 7, 1, 8);
	check_set_acc_range(emul, ms, 8, 1, 8);
	check_set_acc_range(emul, ms, 9, 1, 16);
	check_set_acc_range(emul, ms, 15, 1, 16);
	check_set_acc_range(emul, ms, 16, 1, 16);
	check_set_acc_range(emul, ms, 17, 1, 16);
}

/**
 * Try to set gyroscope range and check if expected range was set in driver and
 * in emulator.
 */
static void check_set_gyr_range_f(const struct emul *emul,
				  struct motion_sensor_t *ms, int range,
				  int rnd, int exp_range, int line)
{
	uint8_t exp_range_reg;
	uint8_t range_reg;

	zassert_equal(EC_SUCCESS, ms->drv->set_range(ms, range, rnd),
		      "set_range failed; line: %d", line);
	zassert_equal(exp_range, ms->current_range,
		      "Expected range %d, got %d; line %d", exp_range,
		      ms->current_range, line);
	range_reg = bmi_emul_get_reg(emul, BMI160_GYR_RANGE);

	switch (exp_range) {
	case 125:
		exp_range_reg = BMI160_DPS_SEL_125;
		break;
	case 250:
		exp_range_reg = BMI160_DPS_SEL_250;
		break;
	case 500:
		exp_range_reg = BMI160_DPS_SEL_500;
		break;
	case 1000:
		exp_range_reg = BMI160_DPS_SEL_1000;
		break;
	case 2000:
		exp_range_reg = BMI160_DPS_SEL_2000;
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
#define check_set_gyr_range(emul, ms, range, rnd, exp_range) \
	check_set_gyr_range_f(emul, ms, range, rnd, exp_range, __LINE__)

/** Test set gyroscope range with and without I2C errors */
ZTEST_USER(bmi160, test_bmi_gyr_set_range)
{
	struct motion_sensor_t *ms;
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	struct i2c_common_emul_data *common_data =
		emul_bmi_get_i2c_common_data(emul);
	int start_range;

	ms = &motion_sensors[BMI_GYR_SENSOR_ID];

	/* Setup starting range, shouldn't be changed on error */
	start_range = 250;
	ms->current_range = start_range;
	bmi_emul_set_reg(emul, BMI160_GYR_RANGE, BMI160_DPS_SEL_250);
	/* Setup emulator fail on write */
	i2c_common_emul_set_write_fail_reg(common_data, BMI160_GYR_RANGE);

	/* Test fail on write */
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_range(ms, 125, 0));
	zassert_equal(start_range, ms->current_range);
	zassert_equal(BMI160_DPS_SEL_250,
		      bmi_emul_get_reg(emul, BMI160_GYR_RANGE), NULL);
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_range(ms, 125, 1));
	zassert_equal(start_range, ms->current_range);
	zassert_equal(BMI160_DPS_SEL_250,
		      bmi_emul_get_reg(emul, BMI160_GYR_RANGE), NULL);

	/* Do not fail on write */
	i2c_common_emul_set_write_fail_reg(common_data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test setting range with rounding down */
	check_set_gyr_range(emul, ms, 1, 0, 125);
	check_set_gyr_range(emul, ms, 124, 0, 125);
	check_set_gyr_range(emul, ms, 125, 0, 125);
	check_set_gyr_range(emul, ms, 126, 0, 125);
	check_set_gyr_range(emul, ms, 249, 0, 125);
	check_set_gyr_range(emul, ms, 250, 0, 250);
	check_set_gyr_range(emul, ms, 251, 0, 250);
	check_set_gyr_range(emul, ms, 499, 0, 250);
	check_set_gyr_range(emul, ms, 500, 0, 500);
	check_set_gyr_range(emul, ms, 501, 0, 500);
	check_set_gyr_range(emul, ms, 999, 0, 500);
	check_set_gyr_range(emul, ms, 1000, 0, 1000);
	check_set_gyr_range(emul, ms, 1001, 0, 1000);
	check_set_gyr_range(emul, ms, 1999, 0, 1000);
	check_set_gyr_range(emul, ms, 2000, 0, 2000);
	check_set_gyr_range(emul, ms, 2001, 0, 2000);

	/* Test setting range with rounding up */
	check_set_gyr_range(emul, ms, 1, 1, 125);
	check_set_gyr_range(emul, ms, 124, 1, 125);
	check_set_gyr_range(emul, ms, 125, 1, 125);
	check_set_gyr_range(emul, ms, 126, 1, 250);
	check_set_gyr_range(emul, ms, 249, 1, 250);
	check_set_gyr_range(emul, ms, 250, 1, 250);
	check_set_gyr_range(emul, ms, 251, 1, 500);
	check_set_gyr_range(emul, ms, 499, 1, 500);
	check_set_gyr_range(emul, ms, 500, 1, 500);
	check_set_gyr_range(emul, ms, 501, 1, 1000);
	check_set_gyr_range(emul, ms, 999, 1, 1000);
	check_set_gyr_range(emul, ms, 1000, 1, 1000);
	check_set_gyr_range(emul, ms, 1001, 1, 2000);
	check_set_gyr_range(emul, ms, 1999, 1, 2000);
	check_set_gyr_range(emul, ms, 2000, 1, 2000);
	check_set_gyr_range(emul, ms, 2001, 1, 2000);
}

/** Test get resolution of acclerometer and gyroscope sensor */
ZTEST_USER(bmi160, test_bmi_get_resolution)
{
	struct motion_sensor_t *ms;

	/* Test accelerometer */
	ms = &motion_sensors[BMI_ACC_SENSOR_ID];

	/* Resolution should be always 16 bits */
	zassert_equal(16, ms->drv->get_resolution(ms));

	/* Test gyroscope */
	ms = &motion_sensors[BMI_GYR_SENSOR_ID];

	/* Resolution should be always 16 bits */
	zassert_equal(16, ms->drv->get_resolution(ms));
}

/**
 * Try to set accelerometer data rate and check if expected rate was set
 * in driver and in emulator.
 */
static void check_set_acc_rate_f(const struct emul *emul,
				 struct motion_sensor_t *ms, int rate, int rnd,
				 int exp_rate, int line)
{
	uint8_t exp_rate_reg;
	uint8_t rate_reg;
	int drv_rate;

	zassert_equal(EC_SUCCESS, ms->drv->set_data_rate(ms, rate, rnd),
		      "set_data_rate failed; line: %d", line);
	drv_rate = ms->drv->get_data_rate(ms);
	zassert_equal(exp_rate, drv_rate, "Expected rate %d, got %d; line %d",
		      exp_rate, drv_rate, line);
	rate_reg = bmi_emul_get_reg(emul, BMI160_ACC_CONF);
	rate_reg &= BMI_ODR_MASK;

	switch (exp_rate) {
	case 12500:
		exp_rate_reg = 0x5;
		break;
	case 25000:
		exp_rate_reg = 0x6;
		break;
	case 50000:
		exp_rate_reg = 0x7;
		break;
	case 100000:
		exp_rate_reg = 0x8;
		break;
	case 200000:
		exp_rate_reg = 0x9;
		break;
	case 400000:
		exp_rate_reg = 0xa;
		break;
	case 800000:
		exp_rate_reg = 0xb;
		break;
	case 1600000:
		exp_rate_reg = 0xc;
		break;
	default:
		/* Unknown expected rate */
		zassert_unreachable(
			"Expected rate %d not supported by device; line %d",
			exp_rate, line);
		return;
	}

	zassert_equal(exp_rate_reg, rate_reg,
		      "Expected rate reg 0x%x, got 0x%x; line %d", exp_rate_reg,
		      rate_reg, line);
}
#define check_set_acc_rate(emul, ms, rate, rnd, exp_rate) \
	check_set_acc_rate_f(emul, ms, rate, rnd, exp_rate, __LINE__)

/** Test set and get accelerometer rate with and without I2C errors */
ZTEST_USER(bmi160, test_bmi_acc_rate)
{
	struct motion_sensor_t *ms;
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	struct i2c_common_emul_data *common_data =
		emul_bmi_get_i2c_common_data(emul);
	uint8_t reg_rate;
	int pmu_status;
	int drv_rate;

	ms = &motion_sensors[BMI_ACC_SENSOR_ID];

	/* Test setting rate with rounding down */
	check_set_acc_rate(emul, ms, 12500, 0, 12500);
	check_set_acc_rate(emul, ms, 12501, 0, 12500);
	check_set_acc_rate(emul, ms, 24999, 0, 12500);
	check_set_acc_rate(emul, ms, 25000, 0, 25000);
	check_set_acc_rate(emul, ms, 25001, 0, 25000);
	check_set_acc_rate(emul, ms, 49999, 0, 25000);
	check_set_acc_rate(emul, ms, 50000, 0, 50000);
	check_set_acc_rate(emul, ms, 50001, 0, 50000);
	check_set_acc_rate(emul, ms, 99999, 0, 50000);
	check_set_acc_rate(emul, ms, 100000, 0, 100000);
	check_set_acc_rate(emul, ms, 100001, 0, 100000);
	check_set_acc_rate(emul, ms, 199999, 0, 100000);
	check_set_acc_rate(emul, ms, 200000, 0, 200000);
	check_set_acc_rate(emul, ms, 200001, 0, 200000);
	check_set_acc_rate(emul, ms, 399999, 0, 200000);
	/*
	 * We cannot test frequencies from 400000 to 1600000 because
	 * CONFIG_EC_MAX_SENSOR_FREQ_MILLIHZ is set to 250000
	 */

	/* Test setting rate with rounding up */
	check_set_acc_rate(emul, ms, 6251, 1, 12500);
	check_set_acc_rate(emul, ms, 12499, 1, 12500);
	check_set_acc_rate(emul, ms, 12500, 1, 12500);
	check_set_acc_rate(emul, ms, 12501, 1, 25000);
	check_set_acc_rate(emul, ms, 24999, 1, 25000);
	check_set_acc_rate(emul, ms, 25000, 1, 25000);
	check_set_acc_rate(emul, ms, 25001, 1, 50000);
	check_set_acc_rate(emul, ms, 49999, 1, 50000);
	check_set_acc_rate(emul, ms, 50000, 1, 50000);
	check_set_acc_rate(emul, ms, 50001, 1, 100000);
	check_set_acc_rate(emul, ms, 99999, 1, 100000);
	check_set_acc_rate(emul, ms, 100000, 1, 100000);
	check_set_acc_rate(emul, ms, 100001, 1, 200000);
	check_set_acc_rate(emul, ms, 199999, 1, 200000);
	check_set_acc_rate(emul, ms, 200000, 1, 200000);

	/* Test out of range rate with rounding down */
	zassert_equal(EC_RES_INVALID_PARAM, ms->drv->set_data_rate(ms, 1, 0),
		      NULL);
	zassert_equal(EC_RES_INVALID_PARAM,
		      ms->drv->set_data_rate(ms, 12499, 0), NULL);
	zassert_equal(EC_RES_INVALID_PARAM,
		      ms->drv->set_data_rate(ms, 400000, 0), NULL);
	zassert_equal(EC_RES_INVALID_PARAM,
		      ms->drv->set_data_rate(ms, 2000000, 0), NULL);

	/* Test out of range rate with rounding up */
	zassert_equal(EC_RES_INVALID_PARAM, ms->drv->set_data_rate(ms, 1, 1),
		      NULL);
	zassert_equal(EC_RES_INVALID_PARAM, ms->drv->set_data_rate(ms, 6250, 1),
		      NULL);
	zassert_equal(EC_RES_INVALID_PARAM,
		      ms->drv->set_data_rate(ms, 200001, 1), NULL);
	zassert_equal(EC_RES_INVALID_PARAM,
		      ms->drv->set_data_rate(ms, 400000, 1), NULL);
	zassert_equal(EC_RES_INVALID_PARAM,
		      ms->drv->set_data_rate(ms, 2000000, 1), NULL);

	/* Current rate shouldn't be changed on error */
	drv_rate = ms->drv->get_data_rate(ms);
	reg_rate = bmi_emul_get_reg(emul, BMI160_ACC_CONF);

	/* Setup emulator fail on read */
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_ACC_CONF);

	/* Test fail on read */
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_data_rate(ms, 50000, 0),
		      NULL);
	zassert_equal(drv_rate, ms->drv->get_data_rate(ms));
	zassert_equal(reg_rate, bmi_emul_get_reg(emul, BMI160_ACC_CONF));
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_data_rate(ms, 50000, 1),
		      NULL);
	zassert_equal(drv_rate, ms->drv->get_data_rate(ms));
	zassert_equal(reg_rate, bmi_emul_get_reg(emul, BMI160_ACC_CONF));

	/* Do not fail on read */
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Setup emulator fail on write */
	i2c_common_emul_set_write_fail_reg(common_data, BMI160_ACC_CONF);

	/* Test fail on write */
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_data_rate(ms, 50000, 0),
		      NULL);
	zassert_equal(drv_rate, ms->drv->get_data_rate(ms));
	zassert_equal(reg_rate, bmi_emul_get_reg(emul, BMI160_ACC_CONF));
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_data_rate(ms, 50000, 1),
		      NULL);
	zassert_equal(drv_rate, ms->drv->get_data_rate(ms));
	zassert_equal(reg_rate, bmi_emul_get_reg(emul, BMI160_ACC_CONF));

	/* Do not fail on write */
	i2c_common_emul_set_write_fail_reg(common_data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test disabling sensor */
	pmu_status = BMI160_PMU_NORMAL << BMI160_PMU_ACC_OFFSET;
	pmu_status |= BMI160_PMU_NORMAL << BMI160_PMU_GYR_OFFSET;
	bmi_emul_set_reg(emul, BMI160_PMU_STATUS, pmu_status);
	zassert_equal(EC_SUCCESS, ms->drv->set_data_rate(ms, 0, 0));

	bmi_read8(ms->port, ms->i2c_spi_addr_flags, BMI160_PMU_STATUS,
		  &pmu_status);
	zassert_equal(BMI160_PMU_NORMAL << BMI160_PMU_GYR_OFFSET, pmu_status,
		      "Gyroscope should be still enabled");

	/* Test enabling sensor */
	bmi_emul_set_reg(emul, BMI160_PMU_STATUS, 0);
	zassert_equal(EC_SUCCESS, ms->drv->set_data_rate(ms, 50000, 0));

	bmi_read8(ms->port, ms->i2c_spi_addr_flags, BMI160_PMU_STATUS,
		  &pmu_status);
	zassert_equal(BMI160_PMU_NORMAL << BMI160_PMU_ACC_OFFSET, pmu_status,
		      "Accelerometer should be enabled");
}

/**
 * Try to set gyroscope data rate and check if expected rate was set
 * in driver and in emulator.
 */
static void check_set_gyr_rate_f(const struct emul *emul,
				 struct motion_sensor_t *ms, int rate, int rnd,
				 int exp_rate, int line)
{
	uint8_t exp_rate_reg;
	uint8_t rate_reg;
	int drv_rate;

	zassert_equal(EC_SUCCESS, ms->drv->set_data_rate(ms, rate, rnd),
		      "set_data_rate failed; line: %d", line);
	drv_rate = ms->drv->get_data_rate(ms);
	zassert_equal(exp_rate, drv_rate, "Expected rate %d, got %d; line %d",
		      exp_rate, drv_rate, line);
	rate_reg = bmi_emul_get_reg(emul, BMI160_GYR_CONF);
	rate_reg &= BMI_ODR_MASK;

	switch (exp_rate) {
	case 25000:
		exp_rate_reg = 0x6;
		break;
	case 50000:
		exp_rate_reg = 0x7;
		break;
	case 100000:
		exp_rate_reg = 0x8;
		break;
	case 200000:
		exp_rate_reg = 0x9;
		break;
	case 400000:
		exp_rate_reg = 0xa;
		break;
	case 800000:
		exp_rate_reg = 0xb;
		break;
	case 1600000:
		exp_rate_reg = 0xc;
		break;
	case 3200000:
		exp_rate_reg = 0xc;
		break;
	default:
		/* Unknown expected rate */
		zassert_unreachable(
			"Expected rate %d not supported by device; line %d",
			exp_rate, line);
		return;
	}

	zassert_equal(exp_rate_reg, rate_reg,
		      "Expected rate reg 0x%x, got 0x%x; line %d", exp_rate_reg,
		      rate_reg, line);
}
#define check_set_gyr_rate(emul, ms, rate, rnd, exp_rate) \
	check_set_gyr_rate_f(emul, ms, rate, rnd, exp_rate, __LINE__)

/** Test set and get gyroscope rate with and without I2C errors */
ZTEST_USER(bmi160, test_bmi_gyr_rate)
{
	struct motion_sensor_t *ms;
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	struct i2c_common_emul_data *common_data =
		emul_bmi_get_i2c_common_data(emul);
	uint8_t reg_rate;
	int pmu_status;
	int drv_rate;

	ms = &motion_sensors[BMI_GYR_SENSOR_ID];

	/* Test setting rate with rounding down */
	check_set_gyr_rate(emul, ms, 25000, 0, 25000);
	check_set_gyr_rate(emul, ms, 25001, 0, 25000);
	check_set_gyr_rate(emul, ms, 49999, 0, 25000);
	check_set_gyr_rate(emul, ms, 50000, 0, 50000);
	check_set_gyr_rate(emul, ms, 50001, 0, 50000);
	check_set_gyr_rate(emul, ms, 99999, 0, 50000);
	check_set_gyr_rate(emul, ms, 100000, 0, 100000);
	check_set_gyr_rate(emul, ms, 100001, 0, 100000);
	check_set_gyr_rate(emul, ms, 199999, 0, 100000);
	check_set_gyr_rate(emul, ms, 200000, 0, 200000);
	check_set_gyr_rate(emul, ms, 200001, 0, 200000);
	check_set_gyr_rate(emul, ms, 399999, 0, 200000);
	/*
	 * We cannot test frequencies from 400000 to 3200000 because
	 * CONFIG_EC_MAX_SENSOR_FREQ_MILLIHZ is set to 250000
	 */

	/* Test setting rate with rounding up */
	check_set_gyr_rate(emul, ms, 12501, 1, 25000);
	check_set_gyr_rate(emul, ms, 24999, 1, 25000);
	check_set_gyr_rate(emul, ms, 25000, 1, 25000);
	check_set_gyr_rate(emul, ms, 25001, 1, 50000);
	check_set_gyr_rate(emul, ms, 49999, 1, 50000);
	check_set_gyr_rate(emul, ms, 50000, 1, 50000);
	check_set_gyr_rate(emul, ms, 50001, 1, 100000);
	check_set_gyr_rate(emul, ms, 99999, 1, 100000);
	check_set_gyr_rate(emul, ms, 100000, 1, 100000);
	check_set_gyr_rate(emul, ms, 100001, 1, 200000);
	check_set_gyr_rate(emul, ms, 199999, 1, 200000);
	check_set_gyr_rate(emul, ms, 200000, 1, 200000);

	/* Test out of range rate with rounding down */
	zassert_equal(EC_RES_INVALID_PARAM, ms->drv->set_data_rate(ms, 1, 0),
		      NULL);
	zassert_equal(EC_RES_INVALID_PARAM,
		      ms->drv->set_data_rate(ms, 24999, 0), NULL);
	zassert_equal(EC_RES_INVALID_PARAM,
		      ms->drv->set_data_rate(ms, 400000, 0), NULL);
	zassert_equal(EC_RES_INVALID_PARAM,
		      ms->drv->set_data_rate(ms, 4000000, 0), NULL);

	/* Test out of range rate with rounding up */
	zassert_equal(EC_RES_INVALID_PARAM, ms->drv->set_data_rate(ms, 1, 1),
		      NULL);
	zassert_equal(EC_RES_INVALID_PARAM,
		      ms->drv->set_data_rate(ms, 12499, 1), NULL);
	zassert_equal(EC_RES_INVALID_PARAM,
		      ms->drv->set_data_rate(ms, 200001, 1), NULL);
	zassert_equal(EC_RES_INVALID_PARAM,
		      ms->drv->set_data_rate(ms, 400000, 1), NULL);
	zassert_equal(EC_RES_INVALID_PARAM,
		      ms->drv->set_data_rate(ms, 4000000, 1), NULL);

	/* Current rate shouldn't be changed on error */
	drv_rate = ms->drv->get_data_rate(ms);
	reg_rate = bmi_emul_get_reg(emul, BMI160_GYR_CONF);

	/* Setup emulator fail on read */
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_GYR_CONF);

	/* Test fail on read */
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_data_rate(ms, 50000, 0),
		      NULL);
	zassert_equal(drv_rate, ms->drv->get_data_rate(ms));
	zassert_equal(reg_rate, bmi_emul_get_reg(emul, BMI160_GYR_CONF));
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_data_rate(ms, 50000, 1),
		      NULL);
	zassert_equal(drv_rate, ms->drv->get_data_rate(ms));
	zassert_equal(reg_rate, bmi_emul_get_reg(emul, BMI160_GYR_CONF));

	/* Do not fail on read */
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Setup emulator fail on write */
	i2c_common_emul_set_write_fail_reg(common_data, BMI160_GYR_CONF);

	/* Test fail on write */
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_data_rate(ms, 50000, 0),
		      NULL);
	zassert_equal(drv_rate, ms->drv->get_data_rate(ms));
	zassert_equal(reg_rate, bmi_emul_get_reg(emul, BMI160_GYR_CONF));
	zassert_equal(EC_ERROR_INVAL, ms->drv->set_data_rate(ms, 50000, 1),
		      NULL);
	zassert_equal(drv_rate, ms->drv->get_data_rate(ms));
	zassert_equal(reg_rate, bmi_emul_get_reg(emul, BMI160_GYR_CONF));

	/* Do not fail on write */
	i2c_common_emul_set_write_fail_reg(common_data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test disabling sensor */
	pmu_status = BMI160_PMU_NORMAL << BMI160_PMU_ACC_OFFSET;
	pmu_status |= BMI160_PMU_NORMAL << BMI160_PMU_GYR_OFFSET;
	bmi_emul_set_reg(emul, BMI160_PMU_STATUS, pmu_status);
	zassert_equal(EC_SUCCESS, ms->drv->set_data_rate(ms, 0, 0));

	bmi_read8(ms->port, ms->i2c_spi_addr_flags, BMI160_PMU_STATUS,
		  &pmu_status);
	zassert_equal(BMI160_PMU_NORMAL << BMI160_PMU_ACC_OFFSET, pmu_status,
		      "Accelerometer should be still enabled");

	/* Test enabling sensor */
	bmi_emul_set_reg(emul, BMI160_PMU_STATUS, 0);
	zassert_equal(EC_SUCCESS, ms->drv->set_data_rate(ms, 50000, 0));

	bmi_read8(ms->port, ms->i2c_spi_addr_flags, BMI160_PMU_STATUS,
		  &pmu_status);
	zassert_equal(BMI160_PMU_NORMAL << BMI160_PMU_GYR_OFFSET, pmu_status,
		      "Gyroscope should be enabled");
}

/**
 * Test setting and getting scale in accelerometer and gyroscope sensors.
 * Correct appling scale to results is checked in "read" test.
 */
ZTEST_USER(bmi160, test_bmi_scale)
{
	struct motion_sensor_t *ms;
	int16_t ret_scale[3];
	int16_t exp_scale[3] = { 100, 231, 421 };
	int16_t t;

	/* Test accelerometer */
	ms = &motion_sensors[BMI_ACC_SENSOR_ID];

	zassert_equal(EC_SUCCESS, ms->drv->set_scale(ms, exp_scale, 0));
	zassert_equal(EC_SUCCESS, ms->drv->get_scale(ms, ret_scale, &t));

	zassert_equal(t, (int16_t)EC_MOTION_SENSE_INVALID_CALIB_TEMP);
	zassert_equal(exp_scale[0], ret_scale[0]);
	zassert_equal(exp_scale[1], ret_scale[1]);
	zassert_equal(exp_scale[2], ret_scale[2]);

	/* Test gyroscope */
	ms = &motion_sensors[BMI_GYR_SENSOR_ID];

	zassert_equal(EC_SUCCESS, ms->drv->set_scale(ms, exp_scale, 0));
	zassert_equal(EC_SUCCESS, ms->drv->get_scale(ms, ret_scale, &t));

	zassert_equal(t, (int16_t)EC_MOTION_SENSE_INVALID_CALIB_TEMP);
	zassert_equal(exp_scale[0], ret_scale[0]);
	zassert_equal(exp_scale[1], ret_scale[1]);
	zassert_equal(exp_scale[2], ret_scale[2]);
}

/** Test reading temperature using accelerometer and gyroscope sensors */
ZTEST_USER(bmi160, test_bmi_read_temp)
{
	struct motion_sensor_t *ms_acc, *ms_gyr;
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	struct i2c_common_emul_data *common_data =
		emul_bmi_get_i2c_common_data(emul);
	int ret_temp;
	int exp_temp;

	ms_acc = &motion_sensors[BMI_ACC_SENSOR_ID];
	ms_gyr = &motion_sensors[BMI_GYR_SENSOR_ID];

	/* Setup emulator fail on read */
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_TEMPERATURE_0);
	zassert_equal(EC_ERROR_NOT_POWERED,
		      ms_acc->drv->read_temp(ms_acc, &ret_temp), NULL);
	zassert_equal(EC_ERROR_NOT_POWERED,
		      ms_gyr->drv->read_temp(ms_gyr, &ret_temp), NULL);
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_TEMPERATURE_1);
	zassert_equal(EC_ERROR_NOT_POWERED,
		      ms_acc->drv->read_temp(ms_acc, &ret_temp), NULL);
	zassert_equal(EC_ERROR_NOT_POWERED,
		      ms_gyr->drv->read_temp(ms_gyr, &ret_temp), NULL);
	/* Do not fail on read */
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Fail on invalid temperature */
	bmi_emul_set_reg(emul, BMI160_TEMPERATURE_0, 0x00);
	bmi_emul_set_reg(emul, BMI160_TEMPERATURE_1, 0x80);
	zassert_equal(EC_ERROR_NOT_POWERED,
		      ms_acc->drv->read_temp(ms_acc, &ret_temp), NULL);
	zassert_equal(EC_ERROR_NOT_POWERED,
		      ms_gyr->drv->read_temp(ms_gyr, &ret_temp), NULL);

	/*
	 * Test correct values. Both motion sensors should return the same
	 * temperature.
	 */
	exp_temp = C_TO_K(23);
	bmi_emul_set_reg(emul, BMI160_TEMPERATURE_0, 0x00);
	bmi_emul_set_reg(emul, BMI160_TEMPERATURE_1, 0x00);
	zassert_equal(EC_SUCCESS, ms_acc->drv->read_temp(ms_acc, &ret_temp),
		      NULL);
	zassert_equal(exp_temp, ret_temp);
	zassert_equal(EC_SUCCESS, ms_gyr->drv->read_temp(ms_gyr, &ret_temp),
		      NULL);
	zassert_equal(exp_temp, ret_temp);

	exp_temp = C_TO_K(87);
	bmi_emul_set_reg(emul, BMI160_TEMPERATURE_0, 0xff);
	bmi_emul_set_reg(emul, BMI160_TEMPERATURE_1, 0x7f);
	zassert_equal(EC_SUCCESS, ms_acc->drv->read_temp(ms_acc, &ret_temp),
		      NULL);
	zassert_equal(exp_temp, ret_temp);
	zassert_equal(EC_SUCCESS, ms_gyr->drv->read_temp(ms_gyr, &ret_temp),
		      NULL);
	zassert_equal(exp_temp, ret_temp);

	exp_temp = C_TO_K(-41);
	bmi_emul_set_reg(emul, BMI160_TEMPERATURE_0, 0x01);
	bmi_emul_set_reg(emul, BMI160_TEMPERATURE_1, 0x80);
	zassert_equal(EC_SUCCESS, ms_acc->drv->read_temp(ms_acc, &ret_temp),
		      NULL);
	zassert_equal(exp_temp, ret_temp);
	zassert_equal(EC_SUCCESS, ms_gyr->drv->read_temp(ms_gyr, &ret_temp),
		      NULL);
	zassert_equal(exp_temp, ret_temp);

	exp_temp = C_TO_K(47);
	bmi_emul_set_reg(emul, BMI160_TEMPERATURE_0, 0x00);
	bmi_emul_set_reg(emul, BMI160_TEMPERATURE_1, 0x30);
	zassert_equal(EC_SUCCESS, ms_acc->drv->read_temp(ms_acc, &ret_temp),
		      NULL);
	zassert_equal(exp_temp, ret_temp);
	zassert_equal(EC_SUCCESS, ms_gyr->drv->read_temp(ms_gyr, &ret_temp),
		      NULL);
	zassert_equal(exp_temp, ret_temp);
}

/** Test reading accelerometer sensor data */
ZTEST_USER(bmi160, test_bmi_acc_read)
{
	struct motion_sensor_t *ms;
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	struct i2c_common_emul_data *common_data =
		emul_bmi_get_i2c_common_data(emul);
	intv3_t ret_v;
	intv3_t exp_v;
	int16_t scale[3] = { MOTION_SENSE_DEFAULT_SCALE,
			     MOTION_SENSE_DEFAULT_SCALE,
			     MOTION_SENSE_DEFAULT_SCALE };

	ms = &motion_sensors[BMI_ACC_SENSOR_ID];

	/* Set offset 0 to simplify test */
	bmi_emul_set_off(emul, BMI_EMUL_ACC_X, 0);
	bmi_emul_set_off(emul, BMI_EMUL_ACC_Y, 0);
	bmi_emul_set_off(emul, BMI_EMUL_ACC_Z, 0);

	/* Fail on read status */
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_STATUS);
	zassert_equal(EC_ERROR_INVAL, ms->drv->read(ms, ret_v));

	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* When not ready, driver should return saved raw value */
	exp_v[0] = 100;
	exp_v[1] = 200;
	exp_v[2] = 300;
	ms->raw_xyz[0] = exp_v[0];
	ms->raw_xyz[1] = exp_v[1];
	ms->raw_xyz[2] = exp_v[2];

	/* Status not ready */
	bmi_emul_set_reg(emul, BMI160_STATUS, 0);
	zassert_equal(EC_SUCCESS, ms->drv->read(ms, ret_v));
	compare_int3v(exp_v, ret_v);

	/* Status only GYR ready */
	bmi_emul_set_reg(emul, BMI160_STATUS, BMI160_DRDY_GYR);
	zassert_equal(EC_SUCCESS, ms->drv->read(ms, ret_v));
	compare_int3v(exp_v, ret_v);

	/* Status ACC ready */
	bmi_emul_set_reg(emul, BMI160_STATUS, BMI160_DRDY_ACC);

	/* Set input accelerometer values */
	exp_v[0] = BMI_EMUL_1G / 10;
	exp_v[1] = BMI_EMUL_1G / 20;
	exp_v[2] = -(int)BMI_EMUL_1G / 30;
	set_emul_acc(emul, exp_v);
	/* Disable rotation */
	ms->rot_standard_ref = NULL;
	/* Set scale */
	zassert_equal(EC_SUCCESS, ms->drv->set_scale(ms, scale, 0));
	/* Set range to 2G */
	zassert_equal(EC_SUCCESS, ms->drv->set_range(ms, 2, 0));

	/* Test read without rotation */
	zassert_equal(EC_SUCCESS, ms->drv->read(ms, ret_v));
	drv_acc_to_emul(ret_v, 2, ret_v);
	compare_int3v(exp_v, ret_v);

	/* Set range to 4G */
	zassert_equal(EC_SUCCESS, ms->drv->set_range(ms, 4, 0));

	/* Test read without rotation */
	zassert_equal(EC_SUCCESS, ms->drv->read(ms, ret_v));
	drv_acc_to_emul(ret_v, 4, ret_v);
	compare_int3v(exp_v, ret_v);

	/* Setup rotation and rotate expected vector */
	ms->rot_standard_ref = &test_rotation;
	rotate_int3v_by_test_rotation(exp_v);
	/* Set range to 2G */
	zassert_equal(EC_SUCCESS, ms->drv->set_range(ms, 2, 0));

	/* Test read with rotation */
	zassert_equal(EC_SUCCESS, ms->drv->read(ms, ret_v));
	drv_acc_to_emul(ret_v, 2, ret_v);
	compare_int3v(exp_v, ret_v);

	/* Set range to 4G */
	zassert_equal(EC_SUCCESS, ms->drv->set_range(ms, 4, 0));

	/* Test read with rotation */
	zassert_equal(EC_SUCCESS, ms->drv->read(ms, ret_v));
	drv_acc_to_emul(ret_v, 4, ret_v);
	compare_int3v(exp_v, ret_v);

	/* Fail on read of data registers */
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_ACC_X_L_G);
	zassert_equal(EC_ERROR_INVAL, ms->drv->read(ms, ret_v));
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_ACC_X_H_G);
	zassert_equal(EC_ERROR_INVAL, ms->drv->read(ms, ret_v));
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_ACC_Y_L_G);
	zassert_equal(EC_ERROR_INVAL, ms->drv->read(ms, ret_v));
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_ACC_Y_H_G);
	zassert_equal(EC_ERROR_INVAL, ms->drv->read(ms, ret_v));
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_ACC_Z_L_G);
	zassert_equal(EC_ERROR_INVAL, ms->drv->read(ms, ret_v));
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_ACC_Z_H_G);
	zassert_equal(EC_ERROR_INVAL, ms->drv->read(ms, ret_v));

	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	ms->rot_standard_ref = NULL;
}

/** Test reading gyroscope sensor data */
ZTEST_USER(bmi160, test_bmi_gyr_read)
{
	struct motion_sensor_t *ms;
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	struct i2c_common_emul_data *common_data =
		emul_bmi_get_i2c_common_data(emul);
	intv3_t ret_v;
	intv3_t exp_v;
	int16_t scale[3] = { MOTION_SENSE_DEFAULT_SCALE,
			     MOTION_SENSE_DEFAULT_SCALE,
			     MOTION_SENSE_DEFAULT_SCALE };

	ms = &motion_sensors[BMI_GYR_SENSOR_ID];

	/* Set offset 0 to simplify test */
	bmi_emul_set_off(emul, BMI_EMUL_GYR_X, 0);
	bmi_emul_set_off(emul, BMI_EMUL_GYR_Y, 0);
	bmi_emul_set_off(emul, BMI_EMUL_GYR_Z, 0);

	/* Fail on read status */
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_STATUS);
	zassert_equal(EC_ERROR_INVAL, ms->drv->read(ms, ret_v));

	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* When not ready, driver should return saved raw value */
	exp_v[0] = 100;
	exp_v[1] = 200;
	exp_v[2] = 300;
	ms->raw_xyz[0] = exp_v[0];
	ms->raw_xyz[1] = exp_v[1];
	ms->raw_xyz[2] = exp_v[2];

	/* Status not ready */
	bmi_emul_set_reg(emul, BMI160_STATUS, 0);
	zassert_equal(EC_SUCCESS, ms->drv->read(ms, ret_v));
	compare_int3v(exp_v, ret_v);

	/* Status only ACC ready */
	bmi_emul_set_reg(emul, BMI160_STATUS, BMI160_DRDY_ACC);
	zassert_equal(EC_SUCCESS, ms->drv->read(ms, ret_v));
	compare_int3v(exp_v, ret_v);

	/* Status GYR ready */
	bmi_emul_set_reg(emul, BMI160_STATUS, BMI160_DRDY_GYR);

	/* Set input accelerometer values */
	exp_v[0] = BMI_EMUL_125_DEG_S / 10;
	exp_v[1] = BMI_EMUL_125_DEG_S / 20;
	exp_v[2] = -(int)BMI_EMUL_125_DEG_S / 30;
	set_emul_gyr(emul, exp_v);
	/* Disable rotation */
	ms->rot_standard_ref = NULL;
	/* Set scale */
	zassert_equal(EC_SUCCESS, ms->drv->set_scale(ms, scale, 0));
	/* Set range to 125°/s */
	zassert_equal(EC_SUCCESS, ms->drv->set_range(ms, 125, 0));

	/* Test read without rotation */
	zassert_equal(EC_SUCCESS, ms->drv->read(ms, ret_v));
	drv_gyr_to_emul(ret_v, 125, ret_v);
	compare_int3v(exp_v, ret_v);

	/* Set range to 1000°/s */
	zassert_equal(EC_SUCCESS, ms->drv->set_range(ms, 1000, 0));

	/* Test read without rotation */
	zassert_equal(EC_SUCCESS, ms->drv->read(ms, ret_v));
	drv_gyr_to_emul(ret_v, 1000, ret_v);
	compare_int3v(exp_v, ret_v);

	/* Setup rotation and rotate expected vector */
	ms->rot_standard_ref = &test_rotation;
	rotate_int3v_by_test_rotation(exp_v);
	/* Set range to 125°/s */
	zassert_equal(EC_SUCCESS, ms->drv->set_range(ms, 125, 0));

	/* Test read with rotation */
	zassert_equal(EC_SUCCESS, ms->drv->read(ms, ret_v));
	drv_gyr_to_emul(ret_v, 125, ret_v);
	compare_int3v(exp_v, ret_v);

	/* Set range to 1000°/s */
	zassert_equal(EC_SUCCESS, ms->drv->set_range(ms, 1000, 0));

	/* Test read with rotation */
	zassert_equal(EC_SUCCESS, ms->drv->read(ms, ret_v));
	drv_gyr_to_emul(ret_v, 1000, ret_v);
	compare_int3v(exp_v, ret_v);

	/* Fail on read of data registers */
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_GYR_X_L_G);
	zassert_equal(EC_ERROR_INVAL, ms->drv->read(ms, ret_v));
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_GYR_X_H_G);
	zassert_equal(EC_ERROR_INVAL, ms->drv->read(ms, ret_v));
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_GYR_Y_L_G);
	zassert_equal(EC_ERROR_INVAL, ms->drv->read(ms, ret_v));
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_GYR_Y_H_G);
	zassert_equal(EC_ERROR_INVAL, ms->drv->read(ms, ret_v));
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_GYR_Z_L_G);
	zassert_equal(EC_ERROR_INVAL, ms->drv->read(ms, ret_v));
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_GYR_Z_H_G);
	zassert_equal(EC_ERROR_INVAL, ms->drv->read(ms, ret_v));

	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	ms->rot_standard_ref = NULL;
}

/**
 * Custom emulatro read function which always return not ready STATUS register.
 * Used in calibration test.
 */
static int emul_nrdy(const struct emul *emul, int reg, uint8_t *val, int byte,
		     void *data)
{
	if (reg == BMI160_STATUS) {
		bmi_emul_set_reg(emul, BMI160_STATUS, 0);
		*val = 0;

		return 0;
	}

	return 1;
}

/** Test acceleromtere calibration */
ZTEST_USER(bmi160, test_bmi_acc_perform_calib)
{
	struct motion_sensor_t *ms;
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	struct i2c_common_emul_data *common_data =
		emul_bmi_get_i2c_common_data(emul);
	uint8_t pmu_status;
	intv3_t start_off;
	intv3_t exp_off;
	intv3_t ret_off;
	int range;
	int rate;
	mat33_fp_t rot = { { FLOAT_TO_FP(1), 0, 0 },
			   { 0, FLOAT_TO_FP(1), 0 },
			   { 0, 0, FLOAT_TO_FP(-1) } };

	ms = &motion_sensors[BMI_ACC_SENSOR_ID];

	/* Enable sensors */
	pmu_status = BMI160_PMU_NORMAL << BMI160_PMU_ACC_OFFSET;
	pmu_status |= BMI160_PMU_NORMAL << BMI160_PMU_GYR_OFFSET;
	bmi_emul_set_reg(emul, BMI160_PMU_STATUS, pmu_status);

	/* Range and rate cannot change after calibration */
	range = 4;
	rate = 50000;
	zassert_equal(EC_SUCCESS, ms->drv->set_range(ms, range, 0));
	zassert_equal(EC_SUCCESS, ms->drv->set_data_rate(ms, rate, 0));

	/* Set offset 0 */
	start_off[0] = 0;
	start_off[1] = 0;
	start_off[2] = 0;
	set_emul_acc_offset(emul, start_off);

	/* Set input accelerometer values */
	exp_off[0] = BMI_EMUL_1G / 10;
	exp_off[1] = BMI_EMUL_1G / 20;
	exp_off[2] = BMI_EMUL_1G - (int)BMI_EMUL_1G / 30;
	set_emul_acc(emul, exp_off);

	/*
	 * Expected offset is [-X, -Y, 1G - Z] for no rotation or positive
	 * rotation on Z axis
	 */
	exp_off[0] = -exp_off[0];
	exp_off[1] = -exp_off[1];
	exp_off[2] = BMI_EMUL_1G - exp_off[2];

	/* Test fail on rate set */
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_ACC_CONF);
	zassert_equal(EC_ERROR_INVAL, ms->drv->perform_calib(ms, 1));
	zassert_equal(range, ms->current_range);
	zassert_equal(rate, ms->drv->get_data_rate(ms));

	/* Test fail on status read */
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_STATUS);
	zassert_equal(EC_ERROR_INVAL, ms->drv->perform_calib(ms, 1));
	zassert_equal(range, ms->current_range);
	zassert_equal(rate, ms->drv->get_data_rate(ms));
	/* Stop fast offset compensation before next test */
	bmi_emul_set_reg(emul, BMI160_CMD_REG, BMI160_CMD_NOOP);

	/* Test fail on data not ready */
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_read_func(common_data, emul_nrdy, NULL);
	zassert_equal(EC_RES_TIMEOUT, ms->drv->perform_calib(ms, 1));
	zassert_equal(range, ms->current_range);
	zassert_equal(rate, ms->drv->get_data_rate(ms));
	/* Remove custom emulator read function */
	i2c_common_emul_set_read_func(common_data, NULL, NULL);
	/* Stop fast offset compensation before next test */
	bmi_emul_set_reg(emul, BMI160_CMD_REG, BMI160_CMD_NOOP);

	/* Disable rotation */
	ms->rot_standard_ref = NULL;
	/* Test successful offset compenastion without rotation */
	zassert_equal(EC_SUCCESS, ms->drv->perform_calib(ms, 1));
	zassert_equal(range, ms->current_range);
	zassert_equal(rate, ms->drv->get_data_rate(ms));
	get_emul_acc_offset(emul, ret_off);
	/*
	 * Depending on used range, accelerometer values may be up to 6 bits
	 * more accurate then offset value resolution.
	 */
	compare_int3v_eps(exp_off, ret_off, 64);
	/* Acelerometer offset should be enabled */
	zassert_true(bmi_emul_get_reg(emul, BMI160_OFFSET_EN_GYR98) &
			     BMI160_OFFSET_ACC_EN,
		     NULL);

	/* Enable rotation with negative value on Z axis */
	ms->rot_standard_ref = &rot;
	/* Expected offset -1G - accelerometer[Z] */
	bmi_emul_set_value(emul, BMI_EMUL_ACC_Z, -(int)BMI_EMUL_1G - 1234);
	exp_off[2] = 1234;

	/* Test successful offset compenastion with negative Z rotation */
	zassert_equal(EC_SUCCESS, ms->drv->perform_calib(ms, 1));
	zassert_equal(range, ms->current_range);
	zassert_equal(rate, ms->drv->get_data_rate(ms));
	get_emul_acc_offset(emul, ret_off);
	compare_int3v_eps(exp_off, ret_off, 64);
	/* Acelerometer offset should be enabled */
	zassert_true(bmi_emul_get_reg(emul, BMI160_OFFSET_EN_GYR98) &
			     BMI160_OFFSET_ACC_EN,
		     NULL);

	/* Set positive rotation on Z axis */
	rot[2][2] = FLOAT_TO_FP(1);
	/* Expected offset 1G - accelerometer[Z] */
	bmi_emul_set_value(emul, BMI_EMUL_ACC_Z, BMI_EMUL_1G - 1234);
	exp_off[2] = 1234;

	/* Test successful offset compenastion with positive Z rotation */
	zassert_equal(EC_SUCCESS, ms->drv->perform_calib(ms, 1));
	zassert_equal(range, ms->current_range);
	zassert_equal(rate, ms->drv->get_data_rate(ms));
	get_emul_acc_offset(emul, ret_off);
	compare_int3v_eps(exp_off, ret_off, 64);
	/* Acelerometer offset should be enabled */
	zassert_true(bmi_emul_get_reg(emul, BMI160_OFFSET_EN_GYR98) &
			     BMI160_OFFSET_ACC_EN,
		     NULL);
	/* Disable rotation */
	ms->rot_standard_ref = NULL;
}

/** Test gyroscope calibration */
ZTEST_USER(bmi160, test_bmi_gyr_perform_calib)
{
	struct motion_sensor_t *ms;
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	struct i2c_common_emul_data *common_data =
		emul_bmi_get_i2c_common_data(emul);
	uint8_t pmu_status;
	intv3_t start_off;
	intv3_t exp_off;
	intv3_t ret_off;
	int range;
	int rate;

	ms = &motion_sensors[BMI_GYR_SENSOR_ID];

	/* Enable sensors */
	pmu_status = BMI160_PMU_NORMAL << BMI160_PMU_ACC_OFFSET;
	pmu_status |= BMI160_PMU_NORMAL << BMI160_PMU_GYR_OFFSET;
	bmi_emul_set_reg(emul, BMI160_PMU_STATUS, pmu_status);

	/* Range and rate cannot change after calibration */
	range = 250;
	rate = 50000;
	zassert_equal(EC_SUCCESS, ms->drv->set_range(ms, range, 0));
	zassert_equal(EC_SUCCESS, ms->drv->set_data_rate(ms, rate, 0));

	/* Set offset 0 */
	start_off[0] = 0;
	start_off[1] = 0;
	start_off[2] = 0;
	set_emul_gyr_offset(emul, start_off);

	/* Set input accelerometer values */
	exp_off[0] = BMI_EMUL_125_DEG_S / 100;
	exp_off[1] = BMI_EMUL_125_DEG_S / 200;
	exp_off[2] = -(int)BMI_EMUL_125_DEG_S / 300;
	set_emul_gyr(emul, exp_off);

	/* Expected offset is [-X, -Y, -Z] */
	exp_off[0] = -exp_off[0];
	exp_off[1] = -exp_off[1];
	exp_off[2] = -exp_off[2];

	/* Test success on disabling calibration */
	zassert_equal(EC_SUCCESS, ms->drv->perform_calib(ms, 0));
	zassert_equal(range, ms->current_range);
	zassert_equal(rate, ms->drv->get_data_rate(ms));

	/* Test fail on rate set */
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_GYR_CONF);
	zassert_equal(EC_ERROR_INVAL, ms->drv->perform_calib(ms, 1));
	zassert_equal(range, ms->current_range);
	zassert_equal(rate, ms->drv->get_data_rate(ms));

	/* Test fail on status read */
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_STATUS);
	zassert_equal(EC_ERROR_INVAL, ms->drv->perform_calib(ms, 1));
	zassert_equal(range, ms->current_range);
	zassert_equal(rate, ms->drv->get_data_rate(ms));
	/* Stop fast offset compensation before next test */
	bmi_emul_set_reg(emul, BMI160_CMD_REG, BMI160_CMD_NOOP);

	/* Test fail on data not ready */
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_read_func(common_data, emul_nrdy, NULL);
	zassert_equal(EC_RES_TIMEOUT, ms->drv->perform_calib(ms, 1));
	zassert_equal(range, ms->current_range);
	zassert_equal(rate, ms->drv->get_data_rate(ms));
	/* Remove custom emulator read function */
	i2c_common_emul_set_read_func(common_data, NULL, NULL);
	/* Stop fast offset compensation before next test */
	bmi_emul_set_reg(emul, BMI160_CMD_REG, BMI160_CMD_NOOP);

	/* Test successful offset compenastion */
	zassert_equal(EC_SUCCESS, ms->drv->perform_calib(ms, 1));
	zassert_equal(range, ms->current_range);
	zassert_equal(rate, ms->drv->get_data_rate(ms));
	get_emul_gyr_offset(emul, ret_off);
	/*
	 * Depending on used range, gyroscope values may be up to 4 bits
	 * more accurate then offset value resolution.
	 */
	compare_int3v_eps(exp_off, ret_off, 32);
	/* Gyroscope offset should be enabled */
	zassert_true(bmi_emul_get_reg(emul, BMI160_OFFSET_EN_GYR98) &
			     BMI160_OFFSET_GYRO_EN,
		     NULL);
}

/** Test init function of BMI160 accelerometer and gyroscope sensors */
ZTEST_USER(bmi160, test_bmi_init)
{
	struct motion_sensor_t *ms_acc, *ms_gyr;

	ms_acc = &motion_sensors[BMI_ACC_SENSOR_ID];
	ms_gyr = &motion_sensors[BMI_GYR_SENSOR_ID];

	/* Test successful init */
	zassert_equal(EC_RES_SUCCESS, ms_acc->drv->init(ms_acc));

	zassert_equal(EC_RES_SUCCESS, ms_gyr->drv->init(ms_gyr));
}

/** Data for custom emulator read function used in FIFO test */
struct fifo_func_data {
	uint16_t interrupts;
};

/**
 * Custom emulator read function used in FIFO test. It sets interrupt registers
 * to value passed as additional data. It sets interrupt registers to 0 after
 * access.
 */
static int emul_fifo_func(const struct emul *emul, int reg, uint8_t *val,
			  int byte, void *data)
{
	struct fifo_func_data *d = data;

	if (reg + byte == BMI160_INT_STATUS_0) {
		bmi_emul_set_reg(emul, BMI160_INT_STATUS_0,
				 d->interrupts & 0xff);
		d->interrupts &= 0xff00;
	} else if (reg + byte == BMI160_INT_STATUS_1) {
		bmi_emul_set_reg(emul, BMI160_INT_STATUS_1,
				 (d->interrupts >> 8) & 0xff);
		d->interrupts &= 0xff;
	}

	return 1;
}

/**
 * Run irq handler on accelerometer sensor and check if committed data in FIFO
 * match what was set in FIFO frames in emulator.
 */
static void check_fifo_f(struct motion_sensor_t *ms_acc,
			 struct motion_sensor_t *ms_gyr,
			 struct bmi_emul_frame *frame, int acc_range,
			 int gyr_range, int line)
{
	struct ec_response_motion_sensor_data vector;
	struct bmi_emul_frame *f_acc, *f_gyr;
	uint32_t event = BMI_INT_EVENT;
	uint16_t size;
	intv3_t exp_v;
	intv3_t ret_v;

	/* Find first frame of acc and gyr type */
	f_acc = frame;
	while (f_acc != NULL && !(f_acc->type & BMI_EMUL_FRAME_ACC)) {
		f_acc = f_acc->next;
	}

	f_gyr = frame;
	while (f_gyr != NULL && !(f_gyr->type & BMI_EMUL_FRAME_GYR)) {
		f_gyr = f_gyr->next;
	}

	/* Read FIFO in driver */
	zassert_equal(EC_SUCCESS, ms_acc->drv->irq_handler(ms_acc, &event),
		      "Failed to read FIFO in irq handler, line %d", line);

	/* Read all data committed to FIFO */
	while (motion_sense_fifo_read(sizeof(vector), 1, &vector, &size)) {
		/* Ignore timestamp frames */
		if (vector.flags == MOTIONSENSE_SENSOR_FLAG_TIMESTAMP) {
			continue;
		}

		/* Check acclerometer frames */
		if (ms_acc - motion_sensors == vector.sensor_num) {
			if (f_acc == NULL) {
				zassert_unreachable(
					"Not expected acclerometer data in FIFO, line %d",
					line);
			}

			convert_int3v_int16(vector.data, ret_v);
			drv_acc_to_emul(ret_v, acc_range, ret_v);
			exp_v[0] = f_acc->acc_x;
			exp_v[1] = f_acc->acc_y;
			exp_v[2] = f_acc->acc_z;
			compare_int3v_f(exp_v, ret_v, V_EPS, line);
			f_acc = f_acc->next;
		}

		/* Check gyroscope frames */
		if (ms_gyr - motion_sensors == vector.sensor_num) {
			if (f_gyr == NULL) {
				zassert_unreachable(
					"Not expected gyroscope data in FIFO, line %d",
					line);
			}

			convert_int3v_int16(vector.data, ret_v);
			drv_gyr_to_emul(ret_v, gyr_range, ret_v);
			exp_v[0] = f_gyr->gyr_x;
			exp_v[1] = f_gyr->gyr_y;
			exp_v[2] = f_gyr->gyr_z;
			compare_int3v_f(exp_v, ret_v, V_EPS, line);
			f_gyr = f_gyr->next;
		}
	}

	/* Skip frames of different type at the end */
	while (f_acc != NULL && !(f_acc->type & BMI_EMUL_FRAME_ACC)) {
		f_acc = f_acc->next;
	}

	while (f_gyr != NULL && !(f_gyr->type & BMI_EMUL_FRAME_GYR)) {
		f_gyr = f_gyr->next;
	}

	/* All frames are readed */
	zassert_is_null(f_acc, "Not all accelerometer frames are read, line %d",
			line);
	zassert_is_null(f_gyr, "Not all gyroscope frames are read, line %d",
			line);
}
#define check_fifo(ms_acc, ms_gyr, frame, acc_range, gyr_range) \
	check_fifo_f(ms_acc, ms_gyr, frame, acc_range, gyr_range, __LINE__)

/** Test irq handler of accelerometer sensor */
ZTEST_USER(bmi160, test_bmi_acc_fifo)
{
	struct motion_sensor_t *ms, *ms_gyr;
	struct fifo_func_data func_data;
	struct bmi_emul_frame f[3];
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	struct i2c_common_emul_data *common_data =
		emul_bmi_get_i2c_common_data(emul);
	int gyr_range = 125;
	int acc_range = 2;
	int event;

	ms = &motion_sensors[BMI_ACC_SENSOR_ID];
	ms_gyr = &motion_sensors[BMI_GYR_SENSOR_ID];

	/* init bmi before test */
	zassert_equal(EC_RES_SUCCESS, ms->drv->init(ms));
	zassert_equal(EC_RES_SUCCESS, ms_gyr->drv->init(ms_gyr));

	/* Need to be set to collect all data in FIFO */
	ms->oversampling_ratio = 1;
	ms_gyr->oversampling_ratio = 1;
	/* Only BMI event should be handled */
	event = 0x1234 & ~BMI_INT_EVENT;
	zassert_equal(EC_ERROR_NOT_HANDLED, ms->drv->irq_handler(ms, &event),
		      NULL);

	event = BMI_INT_EVENT;

	/* Test fail to read interrupt status registers */
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_INT_STATUS_0);
	zassert_equal(EC_ERROR_INVAL, ms->drv->irq_handler(ms, &event));
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_INT_STATUS_1);
	zassert_equal(EC_ERROR_INVAL, ms->drv->irq_handler(ms, &event));
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test no interrupt */
	bmi_emul_set_reg(emul, BMI160_INT_STATUS_0, 0);
	bmi_emul_set_reg(emul, BMI160_INT_STATUS_1, 0);

	/* Enable sensor FIFO */
	zassert_equal(EC_SUCCESS, ms->drv->set_data_rate(ms, 50000, 0));

	/* Trigger irq handler and check results */
	check_fifo(ms, ms_gyr, NULL, acc_range, gyr_range);

	/* Set custom function for FIFO test */
	i2c_common_emul_set_read_func(common_data, emul_fifo_func, &func_data);
	/* Set range */
	zassert_equal(EC_SUCCESS, ms->drv->set_range(ms, acc_range, 0));
	zassert_equal(EC_SUCCESS, ms_gyr->drv->set_range(ms_gyr, gyr_range, 0),
		      NULL);
	/* Setup single accelerometer frame */
	f[0].type = BMI_EMUL_FRAME_ACC;
	f[0].acc_x = BMI_EMUL_1G / 10;
	f[0].acc_y = BMI_EMUL_1G / 20;
	f[0].acc_z = -(int)BMI_EMUL_1G / 30;
	f[0].next = NULL;
	bmi_emul_append_frame(emul, f);
	/* Setup interrupts register */
	func_data.interrupts = BMI160_FWM_INT;

	/* Trigger irq handler and check results */
	check_fifo(ms, ms_gyr, f, acc_range, gyr_range);

	/* Setup second accelerometer frame */
	f[1].type = BMI_EMUL_FRAME_ACC;
	f[1].acc_x = -(int)BMI_EMUL_1G / 40;
	f[1].acc_y = BMI_EMUL_1G / 50;
	f[1].acc_z = BMI_EMUL_1G / 60;
	f[0].next = &(f[1]);
	f[1].next = NULL;
	bmi_emul_append_frame(emul, f);
	/* Setup interrupts register */
	func_data.interrupts = BMI160_FWM_INT;

	/* Trigger irq handler and check results */
	check_fifo(ms, ms_gyr, f, acc_range, gyr_range);

	/* Enable sensor FIFO */
	zassert_equal(EC_SUCCESS, ms_gyr->drv->set_data_rate(ms_gyr, 50000, 0),
		      NULL);

	/* Setup first gyroscope frame (after two accelerometer frames) */
	f[2].type = BMI_EMUL_FRAME_GYR;
	f[2].gyr_x = -(int)BMI_EMUL_125_DEG_S / 100;
	f[2].gyr_y = BMI_EMUL_125_DEG_S / 200;
	f[2].gyr_z = BMI_EMUL_125_DEG_S / 300;
	f[1].next = &(f[2]);
	f[2].next = NULL;
	bmi_emul_append_frame(emul, f);
	/* Setup interrupts register */
	func_data.interrupts = BMI160_FWM_INT;

	/* Trigger irq handler and check results */
	check_fifo(ms, ms_gyr, f, acc_range, gyr_range);

	/* Setup second accelerometer frame to by gyroscope frame too */
	f[1].type |= BMI_EMUL_FRAME_GYR;
	f[1].gyr_x = -(int)BMI_EMUL_125_DEG_S / 300;
	f[1].gyr_y = BMI_EMUL_125_DEG_S / 400;
	f[1].gyr_z = BMI_EMUL_125_DEG_S / 500;
	bmi_emul_append_frame(emul, f);
	/* Setup interrupts register */
	func_data.interrupts = BMI160_FWM_INT;

	/* Trigger irq handler and check results */
	check_fifo(ms, ms_gyr, f, acc_range, gyr_range);

	/* Skip frame should be ignored by driver */
	bmi_emul_set_skipped_frames(emul, 8);
	bmi_emul_append_frame(emul, f);
	/* Setup interrupts register */
	func_data.interrupts = BMI160_FWM_INT;

	/* Trigger irq handler and check results */
	check_fifo(ms, ms_gyr, f, acc_range, gyr_range);

	/* Setup second frame as an config frame */
	f[1].type = BMI_EMUL_FRAME_CONFIG;
	/* Indicate that accelerometer range changed */
	f[1].config = 0x1;
	bmi_emul_append_frame(emul, f);
	/* Setup interrupts register */
	func_data.interrupts = BMI160_FWM_INT;

	/* Trigger irq handler and check results */
	check_fifo(ms, ms_gyr, f, acc_range, gyr_range);

	/* Remove custom emulator read function */
	i2c_common_emul_set_read_func(common_data, NULL, NULL);
}

/** Test irq handler of gyroscope sensor */
ZTEST_USER(bmi160, test_bmi_gyr_fifo)
{
	struct motion_sensor_t *ms;
	uint32_t event;

	ms = &motion_sensors[BMI_GYR_SENSOR_ID];

	/* Interrupt shuldn't be triggered for gyroscope motion sense */
	event = BMI_INT_EVENT;
	zassert_equal(EC_ERROR_NOT_HANDLED, ms->drv->irq_handler(ms, &event),
		      NULL);
}

/** Test irq handler of accelerometer sensor when interrupt register is stuck.
 */
ZTEST_USER(bmi160, test_bmi_acc_fifo_stuck)
{
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	struct motion_sensor_t *ms_acc = &motion_sensors[BMI_ACC_SENSOR_ID];
	struct motion_sensor_t *ms_gyr = &motion_sensors[BMI_GYR_SENSOR_ID];
	uint32_t event = BMI_INT_EVENT;

	/* init bmi before test */
	zassert_equal(EC_RES_SUCCESS, ms_acc->drv->init(ms_acc));
	zassert_equal(EC_RES_SUCCESS, ms_gyr->drv->init(ms_gyr));

	/* Setup interrupts register */
	bmi_emul_set_reg(emul, BMI160_INT_STATUS_0, BMI160_FWM_INT & 0xff);
	bmi_emul_set_reg(emul, BMI160_INT_STATUS_1,
			 (BMI160_FWM_INT >> 8) & 0xff);

	/* Read FIFO in driver */
	zassert_equal(EC_SUCCESS, ms_acc->drv->irq_handler(ms_acc, &event),
		      "Failed to read FIFO in irq handler");
}

/** Test reading from compass via `bmi160_sec_raw_read8()` */
ZTEST_USER(bmi160, test_bmi_sec_raw_read8)
{
	struct motion_sensor_t *ms = &motion_sensors[BMI_ACC_SENSOR_ID];
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	uint8_t expected_read_value = 0xAA;
	uint8_t requested_reg_addr = 0x55;
	uint8_t actual_reg_addr;
	int actual_read_value;
	int ret;

	bmi_emul_set_reg(emul, BMI160_MAG_I2C_READ_DATA, expected_read_value);

	ret = bmi160_sec_raw_read8(ms->port, ms->i2c_spi_addr_flags,
				   requested_reg_addr, &actual_read_value);

	/* Verify return value */
	zassert_equal(ret, EC_RES_SUCCESS, "Expected return code %d but got %d",
		      EC_RES_SUCCESS, ret);

	/* Verify the correct value was read */
	zassert_equal(expected_read_value, actual_read_value,
		      "Read value $%02x but expected to read $%02x",
		      actual_read_value, expected_read_value);

	/* Verify the intended register address was read */
	actual_reg_addr = bmi_emul_get_reg(emul, BMI160_MAG_I2C_READ_ADDR);
	zassert_equal(requested_reg_addr, actual_reg_addr,
		      "Read reg $%02x but expected to read $%02x",
		      actual_reg_addr, requested_reg_addr);
}

/** Test writing to compass via `bmi160_sec_raw_write8()` */
ZTEST_USER(bmi160, test_bmi_sec_raw_write8)
{
	struct motion_sensor_t *ms = &motion_sensors[BMI_ACC_SENSOR_ID];
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	uint8_t expected_write_value = 0xAB;
	uint8_t requested_reg_addr = 0x56;
	uint8_t actual_reg_addr;
	int actual_written_value;
	int ret;

	ret = bmi160_sec_raw_write8(ms->port, ms->i2c_spi_addr_flags,
				    requested_reg_addr, expected_write_value);

	/* Verify return value */
	zassert_equal(ret, EC_RES_SUCCESS, "Expected return code %d but got %d",
		      EC_RES_SUCCESS, ret);

	/* Verify the correct value was written */
	actual_written_value =
		bmi_emul_get_reg(emul, BMI160_MAG_I2C_WRITE_DATA);
	zassert_equal(expected_write_value, actual_written_value,
		      "Wrote value $%02x but expected to write $%02x",
		      actual_written_value, expected_write_value);

	/* Verify the intended register address was used */
	actual_reg_addr = bmi_emul_get_reg(emul, BMI160_MAG_I2C_WRITE_ADDR);
	zassert_equal(requested_reg_addr, actual_reg_addr,
		      "Wrote reg $%02x but expected to write $%02x",
		      actual_reg_addr, requested_reg_addr);
}

/** Test setting an offset on an invalid sensor type */
ZTEST_USER(bmi160, test_bmi_set_offset_invalid_type)
{
	struct motion_sensor_t ms_fake;
	int ret;
	int16_t unused_offset = 0;
	int16_t temp = 0;

	/* make a copy of the accel motion sensor so we modify its type */
	memcpy(&ms_fake, &motion_sensors[BMI_ACC_SENSOR_ID], sizeof(ms_fake));
	ms_fake.type = MOTIONSENSE_TYPE_MAX;

	ret = ms_fake.drv->set_offset(&ms_fake, &unused_offset, temp);

	zassert_equal(ret, EC_RES_INVALID_PARAM,
		      "Expected return code of %d but got %d",
		      EC_RES_INVALID_PARAM, ret);
}

/** Test performing a calibration on a magnetometer, which is not supported */
ZTEST_USER(bmi160, test_bmi_perform_calib_invalid_type)
{
	struct motion_sensor_t ms_fake;
	int ret;

	/* make a copy of the accel motion sensor so we modify its type */
	memcpy(&ms_fake, &motion_sensors[BMI_ACC_SENSOR_ID], sizeof(ms_fake));
	ms_fake.type = MOTIONSENSE_TYPE_MAG;

	ret = ms_fake.drv->perform_calib(&ms_fake, 1);

	zassert_equal(ret, EC_RES_INVALID_PARAM,
		      "Expected return code of %d but got %d",
		      EC_RES_INVALID_PARAM, ret);
}

/** Test reading the onboard temperature sensor */
ZTEST_USER(bmi160, test_bmi_temp_sensor)
{
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	int ret;

	/* Part 1:
	 * Set up the register so we read 300 Kelvin. 0x0000 is 23 deg C, and
	 * each LSB is 0.5^9 deg C. See BMI160 datasheet for more details.
	 */
	int actual_read_temp_k, expected_temp_k = 300;
	uint16_t temp_reg_value = (K_TO_C(expected_temp_k) - 23) << 9;

	bmi_emul_set_reg(emul, BMI160_TEMPERATURE_0, temp_reg_value & 0xFF);
	bmi_emul_set_reg(emul, BMI160_TEMPERATURE_1, temp_reg_value >> 8);

	/* The output will be in Kelvin */
	ret = bmi160_get_sensor_temp(BMI_ACC_SENSOR_ID, &actual_read_temp_k);

	zassert_equal(ret, EC_RES_SUCCESS, "Expected %d but got %d",
		      EC_RES_SUCCESS, ret);
	zassert_equal(expected_temp_k, actual_read_temp_k,
		      "Expected %dK but got %dK", expected_temp_k,
		      actual_read_temp_k);

	/* Part 2:
	 * Have the chip return an invalid reading.
	 */
	temp_reg_value = BMI_INVALID_TEMP;
	bmi_emul_set_reg(emul, BMI160_TEMPERATURE_0, temp_reg_value & 0xFF);
	bmi_emul_set_reg(emul, BMI160_TEMPERATURE_1, temp_reg_value >> 8);

	ret = bmi160_get_sensor_temp(BMI_ACC_SENSOR_ID, &actual_read_temp_k);

	zassert_equal(ret, EC_ERROR_NOT_POWERED, "Expected %d but got %d",
		      EC_ERROR_NOT_POWERED, ret);
}

ZTEST_USER(bmi160, test_bmi_interrupt_handler)
{
	/* The accelerometer interrupt handler simply sets an event flag for the
	 * motion sensing task. Make sure that flag starts cleared, fire the
	 * interrupt, and ensure the flag is set.
	 */

	atomic_t *mask;

	mask = task_get_event_bitmap(TASK_ID_MOTIONSENSE);
	zassert_true(mask != NULL,
		     "Got a null pointer when getting event bitmap.");
	zassert_true((*mask & CONFIG_ACCELGYRO_BMI160_INT_EVENT) == 0,
		     "Event flag is set before firing interrupt");

	bmi160_interrupt(0);

	mask = task_get_event_bitmap(TASK_ID_MOTIONSENSE);
	zassert_true(mask != NULL,
		     "Got a null pointer when getting event bitmap.");
	zassert_true(*mask & CONFIG_ACCELGYRO_BMI160_INT_EVENT,
		     "Event flag is not set after firing interrupt");
}

/* Make an I2C emulator mock wrapped in FFF for use with test_bmi_init_chip_id()
 */
FAKE_VALUE_FUNC(int, bmi_init_chip_id_mock_write_fn, const struct emul *, int,
		uint8_t, int, void *);

/** Test handling of invalid or unreadable chip IDs in init() */
ZTEST_USER(bmi160, test_bmi_init_chip_id)
{
	struct motion_sensor_t *ms = &motion_sensors[BMI_ACC_SENSOR_ID];
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	struct i2c_common_emul_data *common_data =
		emul_bmi_get_i2c_common_data(emul);
	int ret;

	/* Part 1: Cannot read the Chip ID register */
	i2c_common_emul_set_read_fail_reg(common_data, BMI160_CHIP_ID);
	ret = ms->drv->init(ms);

	zassert_equal(ret, EC_ERROR_UNKNOWN, "Expected %d but got %d",
		      EC_ERROR_UNKNOWN, ret);

	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Part 2: Incorrect chip ID - this triggers a series of writes in an
	 * attempt to 'unlock' the chip.
	 */

	/* Have the mocked write function return 1 so everything is passed
	 * through. We only care about using FFF to capture the argument
	 * history.
	 */

	RESET_FAKE(bmi_init_chip_id_mock_write_fn);
	bmi_init_chip_id_mock_write_fn_fake.return_val = 1;
	i2c_common_emul_set_write_func(common_data,
				       bmi_init_chip_id_mock_write_fn, NULL);

	/* Return a phony chip ID */
	bmi_emul_set_reg(emul, BMI160_CHIP_ID, 0xFF);

	ret = ms->drv->init(ms);

	/* Verify return value */
	zassert_equal(ret, EC_ERROR_ACCESS_DENIED, "Expected %d but got %d",
		      EC_ERROR_ACCESS_DENIED, ret);

	/* Verify that all expected I2C writes were completed, in order */
	MOCK_ASSERT_I2C_WRITE(bmi_init_chip_id_mock_write_fn, 0, BMI160_CMD_REG,
			      BMI160_CMD_EXT_MODE_EN_B0);
	MOCK_ASSERT_I2C_WRITE(bmi_init_chip_id_mock_write_fn, 1, BMI160_CMD_REG,
			      BMI160_CMD_EXT_MODE_EN_B1);
	MOCK_ASSERT_I2C_WRITE(bmi_init_chip_id_mock_write_fn, 2, BMI160_CMD_REG,
			      BMI160_CMD_EXT_MODE_EN_B2);
	MOCK_ASSERT_I2C_WRITE(bmi_init_chip_id_mock_write_fn, 3,
			      BMI160_CMD_EXT_MODE_ADDR, BMI160_CMD_PAGING_EN);
	MOCK_ASSERT_I2C_WRITE(bmi_init_chip_id_mock_write_fn, 4,
			      BMI160_CMD_EXT_MODE_ADDR, 0);

	i2c_common_emul_set_write_func(common_data, NULL, NULL);
}

static void bmi160_before(void *fixture)
{
	ARG_UNUSED(fixture);
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	struct i2c_common_emul_data *common_data =
		emul_bmi_get_i2c_common_data(emul);
	struct motion_sensor_t *acc_ms;
	struct motion_sensor_t *gyr_ms;

	acc_ms = &motion_sensors[BMI_ACC_SENSOR_ID];
	gyr_ms = &motion_sensors[BMI_GYR_SENSOR_ID];

	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	bmi_emul_set_reg(emul, BMI160_CHIP_ID, 0xd1);

	/* Disable rotation */
	gyr_ms->rot_standard_ref = NULL;
	acc_ms->rot_standard_ref = NULL;

	zassert_equal(EC_SUCCESS, acc_ms->drv->set_data_rate(acc_ms, 50000, 0),
		      NULL);
	zassert_equal(EC_SUCCESS, gyr_ms->drv->set_data_rate(gyr_ms, 50000, 0),
		      NULL);
}

static void bmi160_after(void *state)
{
	ARG_UNUSED(state);
	struct motion_sensor_t *acc_ms, *gyr_ms;

	acc_ms = &motion_sensors[BMI_ACC_SENSOR_ID];
	gyr_ms = &motion_sensors[BMI_GYR_SENSOR_ID];

	acc_ms->drv->set_data_rate(acc_ms, 0, 0);
	gyr_ms->drv->set_data_rate(gyr_ms, 0, 0);

	gyr_ms->rot_standard_ref = NULL;
	acc_ms->rot_standard_ref = NULL;
}

ZTEST_SUITE(bmi160, drivers_predicate_pre_main, NULL, bmi160_before,
	    bmi160_after, NULL);

/** Cause an interrupt and verify the motion_sense task handled it. */
ZTEST_USER(bmi160_tasks, test_irq_handling)
{
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	struct i2c_common_emul_data *common_data =
		emul_bmi_get_i2c_common_data(emul);
	struct fifo_func_data func_data;
	struct bmi_emul_frame f[3];

	/* Set custom function for FIFO test */
	i2c_common_emul_set_read_func(common_data, emul_fifo_func, &func_data);

	f[0].type = BMI_EMUL_FRAME_ACC;
	f[0].acc_x = BMI_EMUL_1G / 10;
	f[0].acc_y = BMI_EMUL_1G / 20;
	f[0].acc_z = -(int)BMI_EMUL_1G / 30;
	f[0].next = NULL;
	bmi_emul_append_frame(emul, f);
	func_data.interrupts = BMI160_FWM_INT;

	bmi160_interrupt(0);
	k_sleep(K_SECONDS(10));

	/* Verify that the motion_sense_task read it. */
	zassert_equal(bmi_emul_get_reg(emul, BMI160_INT_STATUS_0), 0, NULL);
	zassert_equal(bmi_emul_get_reg(emul, BMI160_INT_STATUS_1), 0, NULL);
}

ZTEST_SUITE(bmi160_tasks, drivers_predicate_post_main, NULL, bmi160_before,
	    bmi160_after, NULL);
