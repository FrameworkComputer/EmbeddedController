/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Bosch Accelerometer driver for Chrome EC
 *
 * Supported: BMA422
 */

#include "accelgyro.h"
#include "accel_bma422.h"
#include "common.h"
#include "console.h"
#include "i2c.h"
#include "math_util.h"
#include "spi.h"
#include "task.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ## args)

/**
 * Read 8bit register from accelerometer.
 */
static inline int bma4_read8(const struct motion_sensor_t *s, const int reg,
			     int *data_ptr)
{
	return i2c_read8(s->port, s->i2c_spi_addr_flags, reg, data_ptr);
}

/**
 * Write 8bit register from accelerometer.
 */
static inline int bma4_write8(const struct motion_sensor_t *s, const int reg,
			      int data)
{
	int ret;

	ret = i2c_write8(s->port, s->i2c_spi_addr_flags, reg, data);

	/*
	 * From Bosch: BMA needs a delay of 450us after each write if it
	 * is in suspend mode, otherwise the operation may be ignored by
	 * the sensor. Given we are only doing write during init, add
	 * the delay unconditionally.
	 */
	usleep(450);

	return ret;
}

/*
 * Set specific bit set to certain value of a 8-bit reg.
 */
static inline int bma4_set_reg8(const struct motion_sensor_t *s, int reg,
				uint8_t bits, int mask)
{
	int val;

	RETURN_ERROR(bma4_read8(s, reg, &val));

	val = (val & ~mask) | bits;

	return bma4_write8(s, reg, val);
}

static int set_range(struct motion_sensor_t *s, int range, int round)
{
	int ret,  range_reg_val;

	range_reg_val = BMA4_RANGE_TO_REG(range);

	/*
	 * If rounding flag is set then set the range_val to nearest
	 * valid value.
	 */
	if ((BMA4_REG_TO_RANGE(range_reg_val) < range) && round)
		range_reg_val = BMA4_RANGE_TO_REG(range * 2);

	mutex_lock(s->mutex);

	/* Determine the new value of control reg and attempt to write it. */
	ret = bma4_set_reg8(s, BMA4_ACCEL_RANGE_ADDR, range_reg_val,
			   BMA4_ACCEL_RANGE_MSK);

	/* If successfully written, then save the range. */
	if (ret == EC_SUCCESS)
		s->current_range = BMA4_REG_TO_RANGE(range_reg_val);

	mutex_unlock(s->mutex);

	return ret;
}

static int get_resolution(const struct motion_sensor_t *s)
{
	return BMA4_12_BIT_RESOLUTION;
}

static int set_data_rate(const struct motion_sensor_t *s, int rate, int round)
{
	int ret, odr_reg_val;
	struct accelgyro_saved_data_t *data = s->drv_data;

	odr_reg_val = BMA4_ODR_TO_REG(rate);

	if ((BMA4_REG_TO_ODR(odr_reg_val) < rate) && round)
		odr_reg_val = BMA4_ODR_TO_REG(rate * 2);

	mutex_lock(s->mutex);

	/* Determine the new value of control reg and attempt to write it. */
	ret = bma4_set_reg8(s, BMA4_ACCEL_CONFIG_ADDR, odr_reg_val,
			   BMA4_ACCEL_ODR_MSK);

	/* If successfully written, then save the new data rate. */
	if (ret == EC_SUCCESS)
		data->odr = BMA4_REG_TO_ODR(odr_reg_val);

	mutex_unlock(s->mutex);

	return ret;
}

static int get_data_rate(const struct motion_sensor_t *s)
{
	struct accelgyro_saved_data_t *data = s->drv_data;

	return data->odr;
}

static int set_offset(const struct motion_sensor_t *s, const int16_t *offset,
		      int16_t temp)
{
	int i, ret;
	intv3_t v = { offset[X], offset[Y], offset[Z] };

	rotate_inv(v, *s->rot_standard_ref, v);

	mutex_lock(s->mutex);

	/* Offset from host is in 1/1024g, 1/128g internally. */
	for (i = X; i <= Z; i++) {
		ret = bma4_write8(s, BMA4_OFFSET_0_ADDR + i, v[i] / 8);

		if (ret) {
			mutex_unlock(s->mutex);
			return ret;
		}
	}

	mutex_unlock(s->mutex);

	return ret;
}

static int get_offset(const struct motion_sensor_t *s, int16_t *offset,
		      int16_t *temp)
{
	int i, val, ret;
	intv3_t v;

	mutex_lock(s->mutex);

	for (i = X; i <= Z; i++) {
		ret = bma4_read8(s, BMA4_OFFSET_0_ADDR + i, &val);

		if (ret) {
			mutex_unlock(s->mutex);
			return ret;
		}

		v[i] = (int8_t)val * 8;
	}

	mutex_unlock(s->mutex);

	rotate(v, *s->rot_standard_ref, v);
	offset[X] = v[X];
	offset[Y] = v[Y];
	offset[Z] = v[Z];

	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;

	return EC_SUCCESS;
}

static int read(const struct motion_sensor_t *s, intv3_t v)
{
	uint8_t acc[6];
	int ret, i;

	mutex_lock(s->mutex);

	/* Read 6 bytes starting at X_AXIS_LSB. */
	ret = i2c_read_block(s->port, s->i2c_spi_addr_flags,
			     BMA4_DATA_8_ADDR, acc, 6);

	mutex_unlock(s->mutex);

	if (ret)
		return ret;

	/*
	 * Convert acceleration to a signed 16-bit number. Note, based on
	 * the order of the registers:
	 *
	 * acc[0] = X_AXIS_LSB -> bit 7~4 for value, bit 0 for new data bit
	 * acc[1] = X_AXIS_MSB
	 * acc[2] = Y_AXIS_LSB -> bit 7~4 for value, bit 0 for new data bit
	 * acc[3] = Y_AXIS_MSB
	 * acc[4] = Z_AXIS_LSB -> bit 7~4 for value, bit 0 for new data bit
	 * acc[5] = Z_AXIS_MSB
	 */
	for (i = X; i <= Z; i++)
		v[i] = (((int8_t)acc[i * 2 + 1]) << 8) | (acc[i * 2] & 0xf0);

	rotate(v, *s->rot_standard_ref, v);

	return EC_SUCCESS;
}

static int perform_calib(struct motion_sensor_t *s, int enable)
{
	/* TODO */
	return EC_ERROR_UNIMPLEMENTED;
}

static int init(struct motion_sensor_t *s)
{
	int ret = 0, reg_val;

	/* This driver requires a mutex. Assert if mutex is not supplied. */
	ASSERT(s->mutex);

	/* Read accelerometer's CHID ID */
	RETURN_ERROR(bma4_read8(s, BMA4_CHIP_ID_ADDR, &reg_val));

	if (s->chip != MOTIONSENSE_CHIP_BMA422 || reg_val != BMA422_CHIP_ID)
		return EC_ERROR_HW_INTERNAL;

	mutex_lock(s->mutex);

	/* Enable accelerometer */
	ret = bma4_set_reg8(s, BMA4_POWER_CTRL_ADDR, BMA4_POWER_ACC_EC_MASK,
			    BMA4_POWER_ACC_EC_MASK);

	mutex_unlock(s->mutex);

	if (ret)
		return ret;

	return sensor_init_done(s);
}

const struct accelgyro_drv bma4_accel_drv = {
	.init = init,
	.read = read,
	.set_range = set_range,
	.get_resolution = get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = get_data_rate,
	.set_offset = set_offset,
	.get_offset = get_offset,
	.perform_calib = perform_calib,
};
