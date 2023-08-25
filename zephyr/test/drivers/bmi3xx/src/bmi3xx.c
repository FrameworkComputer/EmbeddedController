/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "driver/accelgyro_bmi3xx.h"
#include "driver/accelgyro_bmi_common.h"
#include "emul/emul_bmi.h"
#include "emul/emul_common_i2c.h"
#include "i2c.h"
#include "motion_sense_fifo.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define BMI3XX_NODE DT_NODELABEL(bmi3xx_emul)
#define ACC_SENSOR_ID SENSOR_ID(DT_NODELABEL(ms_bmi3xx_accel))
#define GYR_SENSOR_ID SENSOR_ID(DT_NODELABEL(ms_bmi3xx_gyro))

#define BMI_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(SENSOR_ID(DT_ALIAS(bmi3xx_int)))

/** How accurate comparison of vectors should be */
#define V_EPS 8

#define RANGE_SHIFT 4
#define RANGE_MSK 0x7
#define RANGE_2G 0x0
#define RANGE_4G 0x1
#define RANGE_8G 0x2
#define RANGE_16G 0x3
#define RANGE_125DPS 0x0
#define RANGE_250DPS 0x1
#define RANGE_500DPS 0x2
#define RANGE_1000DPS 0x3
#define RANGE_2000DPS 0x4

#define ODR_SHIFT 0
#define ODR_MSK 0xE
#define ODR_800 0xB
#define ODR_1600 0xC

static const struct emul *emul = EMUL_DT_GET(BMI3XX_NODE);
static struct motion_sensor_t *acc = &motion_sensors[ACC_SENSOR_ID];
static struct motion_sensor_t *gyr = &motion_sensors[GYR_SENSOR_ID];

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

static void set_read_fail_reg(struct i2c_common_emul_data *common_data, int reg)
{
	/* turns 16-bit reg to 8-bit. */
	if (reg >= 0)
		reg = REG16TO8(reg);
	common_data->read_fail_reg = reg;
}

/** Rotate given vector by test rotation */
static void rotate_int3v_by_test_rotation(intv3_t v)
{
	int16_t t;

	t = v[0];
	v[0] = -v[1];
	v[1] = t;
	v[2] = -v[2];
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
			exp_v[0], exp_v[1], exp_v[2], exp_v[2], v[0], v[1],
			v[2], line);
	}
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

	if (reg == BMI3_REG_INT_STATUS_INT1) {
		/* skip if it's the first two bytes */
		if (byte <= 1)
			return 1;
		bmi_emul_set_reg16(emul, BMI3_REG_INT_STATUS_INT1,
				   d->interrupts & ((0xff << 8 * (byte - 2))));
		d->interrupts &= (0xff00 >> 8 * (byte - 2));
	}

	return 1;
}

#define compare_int3v_eps(exp_v, v, e) compare_int3v_f(exp_v, v, e, __LINE__)
#define compare_int3v(exp_v, v) compare_int3v_eps(exp_v, v, V_EPS)
static void check_fifo_f(struct motion_sensor_t *ms_acc,
			 struct motion_sensor_t *ms_gyr,
			 struct bmi_emul_frame *frame, int acc_range,
			 int gyr_range, int line)
{
	struct ec_response_motion_sensor_data vector;
	struct bmi_emul_frame *f_acc, *f_gyr;
	uint32_t event = CONFIG_ACCELGYRO_BMI3XX_INT_EVENT;
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

		/* Check accelerometer frames */
		if (ms_acc - motion_sensors == vector.sensor_num) {
			if (f_acc == NULL) {
				zassert_unreachable(
					"Not expected acclerometer data in FIFO"
					", line %d",
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
					"Not expected gyroscope data in FIFO, "
					"line %d",
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

static bool check_sensor_enabled(enum motionsensor_type type)
{
	int reg = bmi_emul_get_reg16(emul, BMI3_REG_FIFO_CONF);

	if (type == MOTIONSENSE_TYPE_ACCEL) {
		return reg & (BMI3_FIFO_ACC_EN << 8);
	} else if (type == MOTIONSENSE_TYPE_GYRO) {
		return reg & (BMI3_FIFO_GYR_EN << 8);
	}

	return false;
}

static void set_emul_acc(const struct emul *emul, intv3_t acc)
{
	bmi_emul_set_value(emul, BMI_EMUL_ACC_X, acc[0]);
	bmi_emul_set_value(emul, BMI_EMUL_ACC_Y, acc[1]);
	bmi_emul_set_value(emul, BMI_EMUL_ACC_Z, acc[2]);
}

/** Set emulator gyroscope values to vector of three int16_t */
static void set_emul_gyr(const struct emul *emul, intv3_t gyr)
{
	bmi_emul_set_value(emul, BMI_EMUL_GYR_X, gyr[0]);
	bmi_emul_set_value(emul, BMI_EMUL_GYR_Y, gyr[1]);
	bmi_emul_set_value(emul, BMI_EMUL_GYR_Z, gyr[2]);
}

/** Test reading accelerometer sensor data */
ZTEST_USER(bmi3xx, test_bmi_acc_read)
{
	struct i2c_common_emul_data *common_data;
	intv3_t ret_v;
	intv3_t exp_v;
	int16_t scale[3] = { MOTION_SENSE_DEFAULT_SCALE,
			     MOTION_SENSE_DEFAULT_SCALE,
			     MOTION_SENSE_DEFAULT_SCALE };

	common_data = emul_bmi_get_i2c_common_data(emul);

	/* Set offset 0 to simplify test */
	bmi_emul_set_off(emul, BMI_EMUL_ACC_X, 0);
	bmi_emul_set_off(emul, BMI_EMUL_ACC_Y, 0);
	bmi_emul_set_off(emul, BMI_EMUL_ACC_Z, 0);

	/* Fail on read status */
	set_read_fail_reg(common_data, BMI3_REG_STATUS);
	zassert_equal(EC_ERROR_INVAL, acc->drv->read(acc, ret_v));

	set_read_fail_reg(common_data, I2C_COMMON_EMUL_NO_FAIL_REG);

	/* When not ready, driver should return saved raw value */
	exp_v[0] = 100;
	exp_v[1] = 200;
	exp_v[2] = 300;
	acc->raw_xyz[0] = exp_v[0];
	acc->raw_xyz[1] = exp_v[1];
	acc->raw_xyz[2] = exp_v[2];

	/* Status not ready */
	bmi_emul_set_reg16(emul, BMI3_REG_STATUS, 0);
	zassert_equal(EC_SUCCESS, acc->drv->read(acc, ret_v));
	compare_int3v(exp_v, ret_v);

	/* Status only GYR ready */
	bmi_emul_set_reg16(emul, BMI3_REG_STATUS,
			   BMI3_DRDY_MASK(MOTIONSENSE_TYPE_GYRO));
	zassert_equal(EC_SUCCESS, acc->drv->read(acc, ret_v));
	compare_int3v(exp_v, ret_v);

	/* Status ACC ready */
	bmi_emul_set_reg16(emul, BMI3_REG_STATUS,
			   BMI3_DRDY_MASK(MOTIONSENSE_TYPE_ACCEL));

	/* Set input accelerometer values */
	exp_v[0] = BMI_EMUL_1G / 10;
	exp_v[1] = BMI_EMUL_1G / 20;
	exp_v[2] = -(int)BMI_EMUL_1G / 30;
	set_emul_acc(emul, exp_v);
	/* Disable rotation */
	acc->rot_standard_ref = NULL;
	/* Set scale */
	zassert_equal(EC_SUCCESS, acc->drv->set_scale(acc, scale, 0));
	/* Set range to 2G */
	zassert_equal(EC_SUCCESS, acc->drv->set_range(acc, 2, 0));

	/* Test read without rotation */
	zassert_equal(EC_SUCCESS, acc->drv->read(acc, ret_v));
	drv_acc_to_emul(ret_v, 2, ret_v);
	compare_int3v(exp_v, ret_v);

	/* Set range to 4G */
	zassert_equal(EC_SUCCESS, acc->drv->set_range(acc, 4, 0));

	/* Status ACC ready */
	bmi_emul_set_reg16(emul, BMI3_REG_STATUS,
			   BMI3_DRDY_MASK(MOTIONSENSE_TYPE_ACCEL));

	/* Test read without rotation */
	zassert_equal(EC_SUCCESS, acc->drv->read(acc, ret_v));
	drv_acc_to_emul(ret_v, 4, ret_v);
	compare_int3v(exp_v, ret_v);

	/* Setup rotation and rotate expected vector */
	acc->rot_standard_ref = &test_rotation;
	rotate_int3v_by_test_rotation(exp_v);
	/* Set range to 2G */
	zassert_equal(EC_SUCCESS, acc->drv->set_range(acc, 2, 0));

	/* Status ACC ready */
	bmi_emul_set_reg16(emul, BMI3_REG_STATUS,
			   BMI3_DRDY_MASK(MOTIONSENSE_TYPE_ACCEL));

	/* Test read with rotation */
	zassert_equal(EC_SUCCESS, acc->drv->read(acc, ret_v));
	drv_acc_to_emul(ret_v, 2, ret_v);
	/* Status ACC ready */
	bmi_emul_set_reg16(emul, BMI3_REG_STATUS,
			   BMI3_DRDY_MASK(MOTIONSENSE_TYPE_ACCEL));
	compare_int3v(exp_v, ret_v);

	/* Set range to 4G */
	zassert_equal(EC_SUCCESS, acc->drv->set_range(acc, 4, 0));

	/* Status ACC ready */
	bmi_emul_set_reg16(emul, BMI3_REG_STATUS,
			   BMI3_DRDY_MASK(MOTIONSENSE_TYPE_ACCEL));

	/* Test read with rotation */
	zassert_equal(EC_SUCCESS, acc->drv->read(acc, ret_v));
	drv_acc_to_emul(ret_v, 4, ret_v);
	compare_int3v(exp_v, ret_v);

	/* Fail on read of data registers */
	bmi_emul_set_reg16(emul, BMI3_REG_STATUS,
			   BMI3_DRDY_MASK(MOTIONSENSE_TYPE_ACCEL));
	set_read_fail_reg(common_data, BMI3_REG_ACC_DATA_X);
	zassert_equal(EC_ERROR_INVAL, acc->drv->read(acc, ret_v));

	bmi_emul_set_reg16(emul, BMI3_REG_STATUS,
			   BMI3_DRDY_MASK(MOTIONSENSE_TYPE_ACCEL));
	set_read_fail_reg(common_data, BMI3_REG_ACC_DATA_Y);
	zassert_equal(EC_ERROR_INVAL, acc->drv->read(acc, ret_v));

	bmi_emul_set_reg16(emul, BMI3_REG_STATUS,
			   BMI3_DRDY_MASK(MOTIONSENSE_TYPE_ACCEL));
	set_read_fail_reg(common_data, BMI3_REG_ACC_DATA_Z);
	zassert_equal(EC_ERROR_INVAL, acc->drv->read(acc, ret_v));

	set_read_fail_reg(common_data, I2C_COMMON_EMUL_NO_FAIL_REG);
	acc->rot_standard_ref = NULL;
}

/** Test reading gyroscope sensor data */
ZTEST_USER(bmi3xx, test_bmi_gyr_read)
{
	struct i2c_common_emul_data *common_data;
	intv3_t ret_v;
	intv3_t exp_v;
	int16_t scale[3] = { MOTION_SENSE_DEFAULT_SCALE,
			     MOTION_SENSE_DEFAULT_SCALE,
			     MOTION_SENSE_DEFAULT_SCALE };

	common_data = emul_bmi_get_i2c_common_data(emul);

	/* Set offset 0 to simplify test */
	bmi_emul_set_off(emul, BMI_EMUL_ACC_X, 0);
	bmi_emul_set_off(emul, BMI_EMUL_ACC_Y, 0);
	bmi_emul_set_off(emul, BMI_EMUL_ACC_Z, 0);

	/* Fail on read status */
	set_read_fail_reg(common_data, BMI3_REG_STATUS);
	zassert_equal(EC_ERROR_INVAL, gyr->drv->read(gyr, ret_v));

	set_read_fail_reg(common_data, I2C_COMMON_EMUL_NO_FAIL_REG);

	/* When not ready, driver should return saved raw value */
	exp_v[0] = 100;
	exp_v[1] = 200;
	exp_v[2] = 300;
	gyr->raw_xyz[0] = exp_v[0];
	gyr->raw_xyz[1] = exp_v[1];
	gyr->raw_xyz[2] = exp_v[2];

	/* Status not ready */
	bmi_emul_set_reg16(emul, BMI3_REG_STATUS, 0);
	zassert_equal(EC_SUCCESS, gyr->drv->read(gyr, ret_v));
	compare_int3v(exp_v, ret_v);

	/* Status only ACC ready */
	bmi_emul_set_reg16(emul, BMI3_REG_STATUS,
			   BMI3_DRDY_MASK(MOTIONSENSE_TYPE_ACCEL));
	zassert_equal(EC_SUCCESS, gyr->drv->read(gyr, ret_v));
	compare_int3v(exp_v, ret_v);

	/* Status GYR ready */
	bmi_emul_set_reg16(emul, BMI3_REG_STATUS,
			   BMI3_DRDY_MASK(MOTIONSENSE_TYPE_GYRO));

	/* Set input accelerometer values */
	exp_v[0] = BMI_EMUL_125_DEG_S / 10;
	exp_v[1] = BMI_EMUL_125_DEG_S / 20;
	exp_v[2] = -(int)BMI_EMUL_125_DEG_S / 30;
	set_emul_gyr(emul, exp_v);
	/* Disable rotation */
	gyr->rot_standard_ref = NULL;
	/* Set scale */
	zassert_equal(EC_SUCCESS, gyr->drv->set_scale(gyr, scale, 0));
	/* Set range to 125°/s */
	zassert_equal(EC_SUCCESS, gyr->drv->set_range(gyr, 125, 0));

	/* Test read without rotation */
	bmi_emul_set_reg16(emul, BMI3_REG_STATUS,
			   BMI3_DRDY_MASK(MOTIONSENSE_TYPE_GYRO));
	zassert_equal(EC_SUCCESS, gyr->drv->read(gyr, ret_v));
	drv_gyr_to_emul(ret_v, 125, ret_v);
	compare_int3v(exp_v, ret_v);

	/* Set range to 1000°/s */
	zassert_equal(EC_SUCCESS, gyr->drv->set_range(gyr, 1000, 0));

	/* Test read without rotation */
	bmi_emul_set_reg16(emul, BMI3_REG_STATUS,
			   BMI3_DRDY_MASK(MOTIONSENSE_TYPE_GYRO));
	zassert_equal(EC_SUCCESS, gyr->drv->read(gyr, ret_v));
	drv_gyr_to_emul(ret_v, 1000, ret_v);
	compare_int3v(exp_v, ret_v);

	/* Setup rotation and rotate expected vector */
	gyr->rot_standard_ref = &test_rotation;
	rotate_int3v_by_test_rotation(exp_v);
	/* Set range to 125°/s */
	zassert_equal(EC_SUCCESS, gyr->drv->set_range(gyr, 125, 0));

	/* Test read with rotation */
	bmi_emul_set_reg16(emul, BMI3_REG_STATUS,
			   BMI3_DRDY_MASK(MOTIONSENSE_TYPE_GYRO));
	zassert_equal(EC_SUCCESS, gyr->drv->read(gyr, ret_v));
	drv_gyr_to_emul(ret_v, 125, ret_v);
	compare_int3v(exp_v, ret_v);

	/* Set range to 1000°/s */
	zassert_equal(EC_SUCCESS, gyr->drv->set_range(gyr, 1000, 0));

	/* Test read with rotation */
	bmi_emul_set_reg16(emul, BMI3_REG_STATUS,
			   BMI3_DRDY_MASK(MOTIONSENSE_TYPE_GYRO));
	zassert_equal(EC_SUCCESS, gyr->drv->read(gyr, ret_v));
	drv_gyr_to_emul(ret_v, 1000, ret_v);
	compare_int3v(exp_v, ret_v);

	/* Fail on read of data registers */
	bmi_emul_set_reg16(emul, BMI3_REG_STATUS,
			   BMI3_DRDY_MASK(MOTIONSENSE_TYPE_GYRO));
	set_read_fail_reg(common_data, BMI3_REG_GYR_DATA_X);
	zassert_equal(EC_ERROR_INVAL, gyr->drv->read(gyr, ret_v));

	bmi_emul_set_reg16(emul, BMI3_REG_STATUS,
			   BMI3_DRDY_MASK(MOTIONSENSE_TYPE_GYRO));
	set_read_fail_reg(common_data, BMI3_REG_GYR_DATA_Y);
	zassert_equal(EC_ERROR_INVAL, gyr->drv->read(gyr, ret_v));

	bmi_emul_set_reg16(emul, BMI3_REG_STATUS,
			   BMI3_DRDY_MASK(MOTIONSENSE_TYPE_GYRO));
	set_read_fail_reg(common_data, BMI3_REG_GYR_DATA_Z);
	zassert_equal(EC_ERROR_INVAL, gyr->drv->read(gyr, ret_v));

	set_read_fail_reg(common_data, I2C_COMMON_EMUL_NO_FAIL_REG);
	gyr->rot_standard_ref = NULL;
}

/** Test irq handler of accelerometer sensor */
ZTEST_USER(bmi3xx, test_bmi_acc_fifo)
{
	struct fifo_func_data func_data;
	struct bmi_emul_frame f[3] = { 0 };
	struct i2c_common_emul_data *common_data =
		emul_bmi_get_i2c_common_data(emul);
	int gyr_range = 125;
	int acc_range = 2;
	int event;

	/* init bmi before test */
	zassert_equal(EC_RES_SUCCESS, acc->drv->init(acc));
	zassert_equal(EC_RES_SUCCESS, gyr->drv->init(gyr));

	/* Need to be set to collect all data in FIFO */
	acc->oversampling_ratio = 1;
	gyr->oversampling_ratio = 1;
	/* Only BMI event should be handled */
	event = 0x1234 & ~BMI_INT_EVENT;
	zassert_equal(EC_ERROR_NOT_HANDLED, acc->drv->irq_handler(acc, &event),
		      NULL);

	event = CONFIG_ACCELGYRO_BMI3XX_INT_EVENT;

	/* Test fail to read interrupt status registers */
	set_read_fail_reg(common_data, BMI3_REG_INT_STATUS_INT1);
	zassert_equal(EC_ERROR_INVAL, acc->drv->irq_handler(acc, &event));
	set_read_fail_reg(common_data, I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test no interrupt */
	bmi_emul_set_reg16(emul, BMI3_REG_INT_STATUS_INT1, 0);

	/* Enable sensor FIFO */
	zassert_equal(EC_SUCCESS, acc->drv->set_data_rate(acc, 50000, 0));

	/* Trigger irq handler and check results */
	check_fifo(acc, gyr, NULL, acc_range, gyr_range);

	/* Set custom function for FIFO test */
	i2c_common_emul_set_read_func(common_data, emul_fifo_func, &func_data);
	/* Set range */
	zassert_equal(EC_SUCCESS, acc->drv->set_range(acc, acc_range, 0));
	zassert_equal(EC_SUCCESS, gyr->drv->set_range(gyr, gyr_range, 0), NULL);
	/* Setup single frame */
	f[0].type = BMI_EMUL_FRAME_ACC;
	f[0].acc_x = BMI_EMUL_1G / 10;
	f[0].acc_y = BMI_EMUL_1G / 20;
	f[0].acc_z = -(int)BMI_EMUL_1G / 30;
	f[0].next = NULL;
	bmi_emul_append_frame(emul, f);
	/* Setup interrupts register */
	func_data.interrupts = BMI3_INT_STATUS_ORIENTATION |
			       BMI3_INT_STATUS_FFULL;

	/* Trigger irq handler and check results */
	check_fifo(acc, gyr, f, acc_range, gyr_range);

	/* Setup second frame */
	f[1].type = BMI_EMUL_FRAME_ACC;
	f[1].acc_x = -(int)BMI_EMUL_1G / 40;
	f[1].acc_y = BMI_EMUL_1G / 50;
	f[1].acc_z = BMI_EMUL_1G / 60;
	f[0].next = &(f[1]);
	f[1].next = NULL;
	bmi_emul_append_frame(emul, f);
	/* Setup interrupts register */
	func_data.interrupts = BMI3_INT_STATUS_ORIENTATION |
			       BMI3_INT_STATUS_FFULL;

	/* Trigger irq handler and check results */
	check_fifo(acc, gyr, f, acc_range, gyr_range);

	/* Enable sensor FIFO */
	zassert_equal(EC_SUCCESS, gyr->drv->set_data_rate(gyr, 50000, 0), NULL);

	f[0].type = BMI_EMUL_FRAME_ACC | BMI_EMUL_FRAME_GYR;
	f[1].type = BMI_EMUL_FRAME_ACC | BMI_EMUL_FRAME_GYR;
	f[2].type = BMI_EMUL_FRAME_ACC | BMI_EMUL_FRAME_GYR;
	f[0].gyr_x = -(int)BMI_EMUL_125_DEG_S / 700;
	f[0].gyr_y = BMI_EMUL_125_DEG_S / 800;
	f[0].gyr_z = BMI_EMUL_125_DEG_S / 900;
	f[1].gyr_x = -(int)BMI_EMUL_125_DEG_S / 400;
	f[1].gyr_y = BMI_EMUL_125_DEG_S / 500;
	f[1].gyr_z = BMI_EMUL_125_DEG_S / 600;
	f[2].acc_x = -(int)BMI_EMUL_1G / 70;
	f[2].acc_y = BMI_EMUL_1G / 80;
	f[2].acc_z = BMI_EMUL_1G / 90;
	f[2].gyr_x = -(int)BMI_EMUL_125_DEG_S / 100;
	f[2].gyr_y = BMI_EMUL_125_DEG_S / 200;
	f[2].gyr_z = BMI_EMUL_125_DEG_S / 300;
	f[1].next = &(f[2]);
	f[2].next = NULL;
	bmi_emul_append_frame(emul, f);
	/* Setup interrupts register */
	func_data.interrupts = BMI3_INT_STATUS_ORIENTATION |
			       BMI3_INT_STATUS_FFULL;

	/* Trigger irq handler and check results */
	check_fifo(acc, gyr, f, acc_range, gyr_range);

	/* Setup the next frame */
	f[1].type |= BMI_EMUL_FRAME_GYR;
	f[1].gyr_x = -(int)BMI_EMUL_125_DEG_S / 300;
	f[1].gyr_y = BMI_EMUL_125_DEG_S / 400;
	f[1].gyr_z = BMI_EMUL_125_DEG_S / 500;
	bmi_emul_append_frame(emul, f);
	/* Setup interrupts register */
	func_data.interrupts = BMI3_INT_STATUS_ORIENTATION |
			       BMI3_INT_STATUS_FFULL;

	/* Trigger irq handler and check results */
	check_fifo(acc, gyr, f, acc_range, gyr_range);

	/* Skip frame should be ignored by driver */
	bmi_emul_set_skipped_frames(emul, 8);
	bmi_emul_append_frame(emul, f);
	/* Setup interrupts register */
	func_data.interrupts = BMI3_INT_STATUS_ORIENTATION |
			       BMI3_INT_STATUS_FFULL;

	/* Trigger irq handler and check results */
	check_fifo(acc, gyr, f, acc_range, gyr_range);

	zassert_equal(EC_SUCCESS, acc->drv->set_data_rate(acc, 0, 0));

	f[0].type = BMI_EMUL_FRAME_GYR;
	f[1].type = BMI_EMUL_FRAME_GYR;
	f[2].type = BMI_EMUL_FRAME_GYR;
	bmi_emul_append_frame(emul, f);
	/* Setup interrupts register */
	func_data.interrupts = BMI3_INT_STATUS_ORIENTATION |
			       BMI3_INT_STATUS_FFULL;

	/* Trigger irq handler and check results */
	check_fifo(acc, gyr, f, acc_range, gyr_range);
}

/** Test irq handler of accelerometer sensor when interrupt register is stuck.
 */
ZTEST_USER(bmi3xx, test_bmi_acc_fifo_stuck)
{
	uint32_t event = CONFIG_ACCELGYRO_BMI3XX_INT_EVENT;

	/* Enable FIFO */
	zassert_equal(EC_SUCCESS, acc->drv->set_data_rate(acc, 50000, 0));

	/* Setup interrupts register */
	bmi_emul_set_reg16(emul, BMI3_REG_INT_STATUS_INT1, BMI3_INT_STATUS_FWM);
	bmi_emul_set_reg16(emul, BMI3_REG_FIFO_CTRL, ~BMI3_ENABLE);

	/* Read FIFO in driver */
	zassert_equal(EC_SUCCESS, acc->drv->irq_handler(acc, &event),
		      "Failed to read FIFO in irq handler");

	zassert_equal(bmi_emul_get_reg16(emul, BMI3_REG_INT_STATUS_INT1),
		      BMI3_INT_STATUS_FWM);
	/* Check flush register has been written to. */
	zassert_equal(bmi_emul_get_reg16(emul, BMI3_REG_FIFO_CTRL) &
			      BMI3_ENABLE,
		      BMI3_ENABLE);
}

ZTEST_USER(bmi3xx, test_bmi_gyr_fifo)
{
	uint32_t event;

	/* Interrupt shuldn't be triggered for gyroscope motion sense */
	event = BMI_INT_EVENT;
	zassert_equal(EC_ERROR_NOT_HANDLED, gyr->drv->irq_handler(gyr, &event),
		      NULL);
}

ZTEST_USER(bmi3xx, test_irq_handler)
{
	struct i2c_common_emul_data *common_data =
		emul_bmi_get_i2c_common_data(emul);
	struct fifo_func_data func_data;
	struct bmi_emul_frame f;

	zassert_ok(acc->drv->init(acc));
	/* Set custom function for FIFO test */
	i2c_common_emul_set_read_func(common_data, emul_fifo_func, &func_data);

	/* test no events */
	bmi3xx_interrupt(0);

	/* test with events */

	f.type = BMI_EMUL_FRAME_ACC;
	f.acc_x = BMI_EMUL_1G / 10;
	f.acc_y = BMI_EMUL_1G / 20;
	f.acc_z = -(int)BMI_EMUL_1G / 30;
	f.next = NULL;
	bmi_emul_append_frame(emul, &f);

	/* Setup interrupts register */
	func_data.interrupts = BMI3_INT_STATUS_ORIENTATION |
			       BMI3_INT_STATUS_FFULL;

	bmi3xx_interrupt(0);

	k_sleep(K_SECONDS(10));

	/* Verify that the motion_sense_task read it. */
	zassert_equal(bmi_emul_get_reg16(emul, BMI3_REG_INT_STATUS_INT1), 0,
		      NULL);
}

ZTEST_USER(bmi3xx, test_read_fifo)
{
	uint32_t event;
	struct bmi_emul_frame f[3];

	f[0].type = BMI_EMUL_FRAME_ACC;
	f[0].acc_x = BMI_EMUL_1G / 10;
	f[0].acc_y = BMI_EMUL_1G / 20;
	f[0].acc_z = -(int)BMI_EMUL_1G / 30;
	f[0].next = NULL;
	bmi_emul_append_frame(emul, f);

	zassert_ok(acc->drv->init(acc));

	f[1].type = BMI_EMUL_FRAME_ACC;
	f[1].acc_x = -(int)BMI_EMUL_1G / 40;
	f[1].acc_y = BMI_EMUL_1G / 50;
	f[1].acc_z = BMI_EMUL_1G / 60;
	f[0].next = &(f[1]);
	f[1].next = NULL;

	/* Setup first gyroscope frame (after two accelerometer frames) */
	f[2].type = BMI_EMUL_FRAME_GYR;
	f[2].gyr_x = -(int)BMI_EMUL_125_DEG_S / 100;
	f[2].gyr_y = BMI_EMUL_125_DEG_S / 200;
	f[2].gyr_z = BMI_EMUL_125_DEG_S / 300;
	f[1].next = &(f[2]);
	f[2].next = NULL;

	/* test events */
	event = CONFIG_ACCELGYRO_BMI3XX_INT_EVENT;

	bmi_emul_append_frame(emul, f);

	bmi_emul_set_reg16(emul, BMI3_REG_INT_STATUS_INT1,
			   BMI3_INT_STATUS_ORIENTATION | BMI3_INT_STATUS_FFULL);
	zassert_ok(acc->drv->irq_handler(acc, &event));
}

ZTEST_USER(bmi3xx, test_perform_calib)
{
	zassert_ok(acc->drv->init(acc));
	zassert_ok(gyr->drv->init(gyr));

	/* test disable */
	zassert_ok(acc->drv->perform_calib(acc, 0));
	zassert_ok(gyr->drv->perform_calib(gyr, 0));

	/* test enable
	 * acc cannot be calibrated
	 */
	zassert_equal(EC_RES_INVALID_COMMAND, acc->drv->perform_calib(acc, 1));

	/* gyr test calib success */
	zassert_ok(gyr->drv->perform_calib(gyr, 1));
}

ZTEST_USER(bmi3xx, test_get_ms_noise)
{
	zassert_ok(acc->drv->init(acc));
	zassert_equal(0, acc->drv->get_rms_noise(acc));
}

ZTEST_USER(bmi3xx, test_offset)
{
	int16_t acc_offset[3], gyr_offset[3];
	/* use multiplies of 32 to avoid rounding error */
	int16_t acc_offset_expected[][3] = { { 32, 32 * 2, 32 * 3 },
					     { -254, 254, -32 } };
	/* calculated input case */
	int16_t gyr_offset_expected[][3] = { { 62, 62 * 2 + 1, 62 * 3 + 1 },
					     { 500, -500, -500 } };
	int16_t acc_temp, gyr_temp;

	zassert_equal(ARRAY_SIZE(acc_offset_expected),
		      ARRAY_SIZE(gyr_offset_expected));

	zassert_ok(acc->drv->init(acc));
	zassert_ok(gyr->drv->init(gyr));

	for (int i = 0; i < ARRAY_SIZE(acc_offset_expected); i++) {
		zassert_ok(
			acc->drv->set_offset(acc, acc_offset_expected[i], 40));
		zassert_ok(
			gyr->drv->set_offset(gyr, gyr_offset_expected[i], 80));
		zassert_ok(acc->drv->get_offset(acc, acc_offset, &acc_temp));
		zassert_ok(gyr->drv->get_offset(gyr, gyr_offset, &gyr_temp));
		zassert_equal(acc_offset[0], acc_offset_expected[i][0]);
		zassert_equal(acc_offset[1], acc_offset_expected[i][1]);
		zassert_equal(acc_offset[2], acc_offset_expected[i][2]);
		zassert_equal(acc_temp, EC_MOTION_SENSE_INVALID_CALIB_TEMP);
		zassert_equal(gyr_offset[0], gyr_offset_expected[i][0]);
		zassert_equal(gyr_offset[1], gyr_offset_expected[i][1]);
		zassert_equal(gyr_offset[2], gyr_offset_expected[i][2]);
		zassert_equal(gyr_temp, EC_MOTION_SENSE_INVALID_CALIB_TEMP);
	}
}

ZTEST_USER(bmi3xx, test_scale)
{
	uint16_t inputs[][3] = {
		{ 0, 0, 0 },
		{ 0, 1, 2 },
		{ 0xffff, 0xfffe, 0xfffd },
		{ 0x00ff, 0x0100, 0x0101 },
		{ 0x01ff, 0x02ff, 0x03ff },
		{ 0, 1, 2 },
		{ 0, 0, 0 },
	};

	/* test acc scale */
	for (int i = 0; i < ARRAY_SIZE(inputs); i++) {
		uint16_t output[3];
		int16_t temp;

		zassert_ok(acc->drv->set_scale(acc, inputs[i], 0));
		zassert_ok(acc->drv->get_scale(acc, output, &temp));
		/* temp is not supported yet */
		zassert_equal(temp, EC_MOTION_SENSE_INVALID_CALIB_TEMP);
		for (int j = 0; j < 3; j++) {
			zassert_equal(output[j], inputs[i][j]);
		}
	}

	/* test gyr scale */
	for (int i = 0; i < ARRAY_SIZE(inputs); i++) {
		uint16_t output[3];
		int16_t temp;

		zassert_ok(gyr->drv->set_scale(gyr, inputs[i], 0));
		zassert_ok(gyr->drv->get_scale(gyr, output, &temp));
		/* temp is not supported yet */
		zassert_equal(temp, EC_MOTION_SENSE_INVALID_CALIB_TEMP);
		for (int j = 0; j < 3; j++) {
			zassert_equal(output[j], inputs[i][j]);
		}
	}
}

ZTEST_USER(bmi3xx, test_date_rate)
{
	zassert_false(check_sensor_enabled(MOTIONSENSE_TYPE_ACCEL));
	zassert_false(check_sensor_enabled(MOTIONSENSE_TYPE_GYRO));

	/* test acc enable */
	zassert_ok(acc->drv->set_data_rate(acc, 12500, 1));
	zassert_true(check_sensor_enabled(MOTIONSENSE_TYPE_ACCEL));
	zassert_false(check_sensor_enabled(MOTIONSENSE_TYPE_GYRO));

	/* test gyr enable */
	zassert_ok(gyr->drv->set_data_rate(gyr, 25000, 1));
	zassert_true(check_sensor_enabled(MOTIONSENSE_TYPE_ACCEL));
	zassert_true(check_sensor_enabled(MOTIONSENSE_TYPE_GYRO));

	/* test gyr disable */
	zassert_ok(gyr->drv->set_data_rate(gyr, 0, 1));
	zassert_true(check_sensor_enabled(MOTIONSENSE_TYPE_ACCEL));
	zassert_false(check_sensor_enabled(MOTIONSENSE_TYPE_GYRO));

	/* test acc disable */
	zassert_ok(acc->drv->set_data_rate(acc, 0, 1));
	zassert_false(check_sensor_enabled(MOTIONSENSE_TYPE_ACCEL));
	zassert_false(check_sensor_enabled(MOTIONSENSE_TYPE_GYRO));

	/* test set fail */
	zassert_ok(!(acc->drv->set_data_rate(acc, 1, 1)));
	zassert_ok(!(gyr->drv->set_data_rate(gyr, 1, 1)));

	/* test get value */
	zassert_equal(0, acc->drv->get_data_rate(acc));
	zassert_equal(0, gyr->drv->get_data_rate(gyr));

	zassert_ok(acc->drv->set_data_rate(acc, 12500, 0));
	zassert_equal(12500, acc->drv->get_data_rate(acc));
	zassert_ok(acc->drv->set_data_rate(acc, 12500, 1));
	zassert_equal(12500, acc->drv->get_data_rate(acc));
	zassert_ok(acc->drv->set_data_rate(acc, 24999, 0));
	zassert_equal(12500, acc->drv->get_data_rate(acc));
	zassert_ok(acc->drv->set_data_rate(acc, 12501, 1));
	zassert_equal(25000, acc->drv->get_data_rate(acc));
	zassert_ok(acc->drv->set_data_rate(acc, 24999, 1));
	zassert_equal(25000, acc->drv->get_data_rate(acc));
	zassert_ok(gyr->drv->set_data_rate(gyr, 25000, 1));
	zassert_equal(25000, gyr->drv->get_data_rate(gyr));

	zassert_ok(acc->drv->set_data_rate(acc, 25000, 0));
	zassert_equal(25000, acc->drv->get_data_rate(acc));
	zassert_ok(gyr->drv->set_data_rate(gyr, 50000, 0));
	zassert_equal(50000, gyr->drv->get_data_rate(gyr));
}

ZTEST_USER(bmi3xx, test_get_resolution)
{
	zassert_equal(acc->drv->get_resolution(acc), 16);
}

ZTEST_USER(bmi3xx, test_set_range)
{
	int old_val, expect_val;
	struct ans {
		int rng;
		int rnd;
		int expect;
	} acci[] = {
		{ 1, 0, RANGE_2G },  { 5, 0, RANGE_4G },  { 5, 1, RANGE_8G },
		{ 16, 0, RANGE_16G }, { 16, 1, RANGE_16G },
	}, gyri[] = {
		{ 1500, 0, RANGE_1000DPS},
		{ 1500, 1, RANGE_2000DPS},
	};

	for (int i = 0; i < ARRAY_SIZE(acci); i++) {
		old_val = bmi_emul_get_reg16(emul, BMI3_REG_ACC_CONF);
		expect_val = (old_val & ~(RANGE_MSK << RANGE_SHIFT)) |
			     (acci[i].expect << RANGE_SHIFT);
		zassert_ok(acc->drv->set_range(acc, acci[i].rng, acci[i].rnd));
		zassert_equal(bmi_emul_get_reg16(emul, BMI3_REG_ACC_CONF),
			      expect_val);
	}

	for (int i = 0; i < ARRAY_SIZE(gyri); i++) {
		old_val = bmi_emul_get_reg16(emul, BMI3_REG_GYR_CONF);
		expect_val = (old_val & ~(RANGE_MSK << RANGE_SHIFT)) |
			     (gyri[i].expect << RANGE_SHIFT);
		zassert_ok(gyr->drv->set_range(gyr, gyri[i].rng, gyri[i].rnd));
		zassert_equal(bmi_emul_get_reg16(emul, BMI3_REG_GYR_CONF),
			      expect_val);
	}
}

ZTEST_USER(bmi3xx, test_read_temp)
{
	int temp;

	zassert_ok(acc->drv->init(acc));

	/* function unimplemented yet */
	zassert_equal(EC_ERROR_UNIMPLEMENTED, acc->drv->read_temp(acc, &temp));
}

ZTEST_USER(bmi3xx, test_init)
{
	/* test init okay */
	zassert_ok(acc->drv->init(acc));
	zassert_ok(gyr->drv->init(gyr));

	/* test invalid ID */
	bmi_emul_set_reg16(emul, BMI3_REG_CHIP_ID, 0x5566);
	zassert_equal(acc->drv->init(acc), EC_ERROR_HW_INTERNAL);
}

static void bmi3xx_before(void *fixture)
{
	struct i2c_common_emul_data *common_data =
		emul_bmi_get_i2c_common_data(emul);

	ARG_UNUSED(fixture);

	bmi_emul_reset(emul);

	set_read_fail_reg(common_data, I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_read_func(common_data, NULL, NULL);

	acc->drv->init(acc);
	gyr->drv->init(gyr);

	memset(acc->raw_xyz, 0, sizeof(intv3_t));
	memset(gyr->raw_xyz, 0, sizeof(intv3_t));
	motion_sense_fifo_reset();
	acc->oversampling_ratio = 1;
	gyr->oversampling_ratio = 1;
}

ZTEST_SUITE(bmi3xx, drivers_predicate_post_main, NULL, bmi3xx_before, NULL,
	    NULL);
