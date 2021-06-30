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

static int write_accel_offset(const struct motion_sensor_t *s, intv3_t v)
{
	int i, val;

	rotate_inv(v, *s->rot_standard_ref, v);

	for (i = X; i <= Z; i++) {
		val = round_divide((int64_t)v[i] * BMA4_OFFSET_ACC_DIV_MG,
				   BMA4_OFFSET_ACC_MULTI_MG);
		if (val > 127)
			val = 127;
		if (val < -128)
			val = -128;
		if (val < 0)
			val += 256;

		RETURN_ERROR(bma4_write8(s, BMA4_OFFSET_0_ADDR + i, val));
	}

	return EC_SUCCESS;
}

static int set_foc_config(struct motion_sensor_t *s)
{
	/* Disabling offset compensation */
	RETURN_ERROR(bma4_set_reg8(s, BMA4_NV_CONFIG_ADDR,
				   (BMA4_DISABLE << BMA4_NV_ACCEL_OFFSET_POS),
				   BMA4_NV_ACCEL_OFFSET_MSK));

	/* Set accelerometer configurations to 50Hz,CIC, continuous mode */
	RETURN_ERROR(bma4_write8(s, BMA4_ACCEL_CONFIG_ADDR,
				 BMA4_FOC_ACC_CONF_VAL));


	/* Set accelerometer to normal mode by enabling it */
	RETURN_ERROR(bma4_set_reg8(s, BMA4_POWER_CTRL_ADDR,
				   (BMA4_ENABLE <<  BMA4_ACCEL_ENABLE_POS),
				   BMA4_ACCEL_ENABLE_MSK));

	/* Disable advance power save mode */
	RETURN_ERROR(bma4_set_reg8(s, BMA4_POWER_CONF_ADDR,
				   (BMA4_DISABLE
				    << BMA4_ADVANCE_POWER_SAVE_POS),
				   BMA4_ADVANCE_POWER_SAVE_MSK));

	return EC_SUCCESS;
}

static int wait_and_read_data(struct motion_sensor_t *s, intv3_t v)
{
	int i;

	/* Retry 5 times */
	uint8_t reg_data[6] = {0}, try_cnt = 5;

	/* Check if data is ready */
	while (try_cnt && (!(reg_data[0] & BMA4_STAT_DATA_RDY_ACCEL_MSK))) {
		/* 20ms delay for 50Hz ODR */
		msleep(20);

		/* Read the status register */
		RETURN_ERROR(i2c_read_block(s->port, s->i2c_spi_addr_flags,
					    BMA4_STATUS_ADDR, reg_data, 1));

		try_cnt--;
	}

	if (!(reg_data[0] & 0x80))
		return EC_ERROR_TIMEOUT;

	/* Read the sensor data */
	RETURN_ERROR(i2c_read_block(s->port, s->i2c_spi_addr_flags,
				    BMA4_DATA_8_ADDR, reg_data, 6));

	for (i = X; i <= Z; i++) {
		v[i] = (((int8_t)reg_data[i * 2 + 1]) << 8)
		       | (reg_data[i * 2] & 0xf0);

		/* Since the resolution is only 12 bits*/
		v[i] = (v[i] / 0x10);
	}

	rotate(v, *s->rot_standard_ref, v);

	return EC_SUCCESS;
}

static int8_t perform_accel_foc(struct motion_sensor_t *s, int *target,
				int sens_range)
{
	intv3_t accel_data, offset;

	/* Structure to store accelerometer data temporarily */
	int32_t delta_value[3] = {0, 0, 0};

	/* Variable to define count */
	uint8_t i, loop, sample_count = 0;

	for (loop = 0; loop < BMA4_FOC_SAMPLE_LIMIT; loop++) {
		RETURN_ERROR(wait_and_read_data(s, accel_data));

		sample_count++;

		/* Store the data in a temporary structure */
		delta_value[0] += accel_data[0] - target[X];
		delta_value[1] += accel_data[1] - target[Y];
		delta_value[2] += accel_data[2] - target[Z];
	}

	/*
	 * The data is in LSB so -> [(LSB)*1000*range/2^11*-1]
	 * (unit of offset:mg)
	 */
	for (i = X; i <= Z; ++i) {
		offset[i] = ((((delta_value[i] * 1000 * sens_range)
			     / sample_count) / 2048) * -1);
	}

	RETURN_ERROR(write_accel_offset(s, offset));

	/* Enable the offsets and backup to NVM */
	RETURN_ERROR(bma4_set_reg8(s, BMA4_NV_CONFIG_ADDR,
				   (BMA4_ENABLE << BMA4_NV_ACCEL_OFFSET_POS),
				   BMA4_NV_ACCEL_OFFSET_MSK));

	return EC_SUCCESS;
}

static int perform_calib(struct motion_sensor_t *s, int enable)
{
	uint8_t config[2];
	int pwr_ctrl, pwr_conf;
	intv3_t target = {0, 0, 0};
	int sens_range = s->current_range;

	if (!enable)
		return EC_SUCCESS;

	/* Read accelerometer configurations */
	RETURN_ERROR(i2c_read_block(s->port, s->i2c_spi_addr_flags,
			BMA4_ACCEL_CONFIG_ADDR, config, 2));

	/* Get accelerometer enable status to be saved */
	RETURN_ERROR(bma4_read8(s, BMA4_POWER_CTRL_ADDR, &pwr_ctrl));

	/* Get advance power save mode to be saved */
	RETURN_ERROR(bma4_read8(s, BMA4_POWER_CONF_ADDR, &pwr_conf));

	/* Perform calibration */
	RETURN_ERROR(set_foc_config(s));

	/* We calibrate considering Z axis is laid flat on the surface */
	target[Z] = BMA4_ACC_DATA_PLUS_1G(sens_range);

	RETURN_ERROR(perform_accel_foc(s, target, sens_range));

	/* Set the saved sensor configuration */
	RETURN_ERROR(i2c_write_block(s->port, s->i2c_spi_addr_flags,
			BMA4_ACCEL_CONFIG_ADDR, config, 2));

	RETURN_ERROR(bma4_write8(s, BMA4_POWER_CTRL_ADDR, pwr_ctrl));

	RETURN_ERROR(bma4_write8(s, BMA4_POWER_CONF_ADDR, pwr_conf));

	return EC_SUCCESS;
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
	ret = bma4_set_reg8(s, BMA4_ACCEL_RANGE_ADDR,
			    range_reg_val << BMA4_ACCEL_RANGE_POS,
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
	ret = bma4_set_reg8(s, BMA4_ACCEL_CONFIG_ADDR,
			    odr_reg_val << BMA4_ACCEL_ODR_POS,
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
	int ret;
	intv3_t v = { offset[X], offset[Y], offset[Z] };

	mutex_lock(s->mutex);

	ret = write_accel_offset(s, v);

	if (ret == EC_SUCCESS) {
		/* Enable the offsets and backup to NVM */
		ret = bma4_set_reg8(s, BMA4_NV_CONFIG_ADDR,
				    (BMA4_ENABLE << BMA4_NV_ACCEL_OFFSET_POS),
				    BMA4_NV_ACCEL_OFFSET_MSK);
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

		if (val > 0x7f)
			val -= -256;

		v[i] = round_divide((int64_t)val * BMA4_OFFSET_ACC_MULTI_MG,
				    BMA4_OFFSET_ACC_DIV_MG);
	}

	mutex_unlock(s->mutex);

	/* Offset is in milli-g */
	rotate(v, *s->rot_standard_ref, v);
	offset[X] = v[X];
	offset[Y] = v[Y];
	offset[Z] = v[Z];

	*temp = (int16_t)EC_MOTION_SENSE_INVALID_CALIB_TEMP;

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
	ret = bma4_set_reg8(s, BMA4_POWER_CTRL_ADDR,
			    BMA4_ENABLE << BMA4_ACCEL_ENABLE_POS,
			    BMA4_ACCEL_ENABLE_MSK);

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
