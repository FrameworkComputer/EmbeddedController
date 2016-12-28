/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * LSM6DSM accelerometer and gyro module for Chrome EC
 * 3D digital accelerometer & 3D digital gyroscope
 */

#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "hooks.h"
#include "i2c.h"
#include "math_util.h"
#include "task.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ## args)

/**
 * @return output base register for sensor
 */
static inline int get_xyz_reg(enum motionsensor_type type)
{
	return LSM6DSM_ACCEL_OUT_X_L_ADDR -
		(LSM6DSM_ACCEL_OUT_X_L_ADDR - LSM6DSM_GYRO_OUT_X_L_ADDR) * type;
}

/**
 * Read single register
 */
static inline int raw_read8(const int port, const int addr, const int reg,
			    int *data_ptr)
{
	return i2c_read8(port, addr, reg, data_ptr);
}

/**
 * Write single register
 */
static inline int raw_write8(const int port, const int addr, const int reg,
			     int data)
{
	return i2c_write8(port, addr, reg, data);
}

 /**
 * write_data_with_mask - Write register with mask
 * @s: Motion sensor pointer
 * @reg: Device register
 * @mask: The mask to search
 * @data: Data pointer
 */
static int write_data_with_mask(const struct motion_sensor_t *s, int reg,
				uint8_t mask, uint8_t data)
{
	int err;
	int new_data = 0x00, old_data = 0x00;

	err = raw_read8(s->port, s->addr, reg, &old_data);
	if (err != EC_SUCCESS)
		return err;

	new_data = ((old_data & (~mask)) | ((data << __builtin_ctz(mask)) & mask));

	if (new_data == old_data)
		return EC_SUCCESS;

	return raw_write8(s->port, s->addr, reg, new_data);
}

/**
 * set_range - set full scale range
 * @s: Motion sensor pointer
 * @range: Range
 * @rnd: Round up/down flag
 */
static int set_range(const struct motion_sensor_t *s, int range, int rnd)
{
	int err;
	uint8_t ctrl_reg, reg_val;
	struct lsm6dsm_data *data = s->drv_data;
	int newrange = range;

	ctrl_reg = LSM6DSM_RANGE_REG(s->type);
	if (s->type == MOTIONSENSE_TYPE_ACCEL) {
		/* Adjust and check rounded value for acc */
		if (rnd && (newrange < LSM6DSM_ACCEL_NORMALIZE_FS(newrange)))
			newrange <<= 1;
		if (newrange > LSM6DSM_ACCEL_FS_MAX_VAL)
			newrange = LSM6DSM_ACCEL_FS_MAX_VAL;

		reg_val = LSM6DSM_ACCEL_FS_REG(newrange);
	} else {
		/* Adjust and check rounded value for gyro */
		if (rnd && (newrange < LSM6DSM_GYRO_NORMALIZE_FS(newrange)))
			newrange <<= 1;
		if (newrange > LSM6DSM_GYRO_FS_MAX_VAL)
			newrange = LSM6DSM_GYRO_FS_MAX_VAL;

		reg_val = LSM6DSM_GYRO_FS_REG(newrange);
	}

	mutex_lock(s->mutex);
	err = write_data_with_mask(s, ctrl_reg, LSM6DSM_RANGE_MASK, reg_val);
	if (err == EC_SUCCESS)
		/* Save internally gain for speed optimization in read polling data */
		data->base.range = (s->type == MOTIONSENSE_TYPE_ACCEL ?
			LSM6DSM_ACCEL_FS_GAIN(newrange) : LSM6DSM_GYRO_FS_GAIN(newrange));
	mutex_unlock(s->mutex);
	return EC_SUCCESS;
}

static int get_range(const struct motion_sensor_t *s)
{
	struct lsm6dsm_data *data = s->drv_data;

	if (MOTIONSENSE_TYPE_ACCEL == s->type)
		return LSM6DSM_ACCEL_GAIN_FS(data->base.range);

	return LSM6DSM_GYRO_GAIN_FS(data->base.range);
}

static int set_resolution(const struct motion_sensor_t *s, int res, int rnd)
{
	/* Only one resolution, LSM6DSM_RESOLUTION, so nothing to do. */
	return EC_SUCCESS;
}

static int get_resolution(const struct motion_sensor_t *s)
{
	/* Only one resolution, LSM6DSM_RESOLUTION, so nothing to do. */
	return LSM6DSM_RESOLUTION;
}

static int set_data_rate(const struct motion_sensor_t *s, int rate, int rnd)
{
	int ret, normalized_rate;
	struct lsm6dsm_data *data = s->drv_data;
	uint8_t ctrl_reg, reg_val;

	reg_val = LSM6DSM_ODR_TO_REG(rate);
	ctrl_reg = LSM6DSM_ODR_REG(s->type);
	normalized_rate = LSM6DSM_ODR_TO_NORMALIZE(rate);

	if (rate == 0) {
		/* Power Off device */
		ret = write_data_with_mask(s, ctrl_reg, LSM6DSM_ODR_MASK,
					   LSM6DSM_ODR_POWER_OFF_VAL);
		return ret;
	}

	if (rnd && (normalized_rate < rate)) {
		reg_val++;
		normalized_rate <<= 1;
	}

	/* Adjust rounded value for acc and gyro because ODR are shared */
	if (reg_val > LSM6DSM_ODR_833HZ_VAL) {
		reg_val = LSM6DSM_ODR_833HZ_VAL;
		normalized_rate = LSM6DSM_ODR_MAX_VAL;
	} else if (reg_val < LSM6DSM_ODR_13HZ_VAL) {
		reg_val = LSM6DSM_ODR_13HZ_VAL;
		normalized_rate = LSM6DSM_ODR_MIN_VAL;
	}

	/*
	 * Lock accel resource to prevent another task from attempting
	 * to write accel parameters until we are done
	 */
	mutex_lock(s->mutex);
	ret = write_data_with_mask(s, ctrl_reg, LSM6DSM_ODR_MASK, reg_val);
	if (ret == EC_SUCCESS)
		data->base.odr = normalized_rate;

	mutex_unlock(s->mutex);
	return ret;
}

static int get_data_rate(const struct motion_sensor_t *s)
{
	struct lsm6dsm_data *data = s->drv_data;

	return data->base.odr;
}

static int set_offset(const struct motion_sensor_t *s,
		      const int16_t *offset, int16_t temp)
{
	struct lsm6dsm_data *data = s->drv_data;

	data->offset[X] = offset[X];
	data->offset[Y] = offset[Y];
	data->offset[Z] = offset[Z];
	return EC_SUCCESS;
}

static int get_offset(const struct motion_sensor_t *s,
		      int16_t *offset,
		      int16_t *temp)
{
	struct lsm6dsm_data *data = s->drv_data;

	offset[X] = data->offset[X];
	offset[Y] = data->offset[Y];
	offset[Z] = data->offset[Z];
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static int is_data_ready(const struct motion_sensor_t *s, int *ready)
{
	int ret, tmp;

	ret = raw_read8(s->port, s->addr, LSM6DSM_STATUS_REG, &tmp);
	if (ret != EC_SUCCESS) {
		CPRINTF("[%T %s type:0x%X RS Error]", s->name, s->type);
		return ret;
	}

	if (MOTIONSENSE_TYPE_ACCEL == s->type)
		*ready = (LSM6DSM_STS_XLDA_UP == (tmp & LSM6DSM_STS_XLDA_MASK));
	else
		*ready = (LSM6DSM_STS_GDA_UP == (tmp & LSM6DSM_STS_GDA_MASK));

	return EC_SUCCESS;
}

/*
 * TODO: Implement FIFO support
 *
 * Is not very efficient to collect the data in read: better have an interrupt
 * and collect the FIFO, even if it has one item: we don't have to check if the
 * sensor is ready (minimize I2C access)
 */
static int read(const struct motion_sensor_t *s, vector_3_t v)
{
	uint8_t raw[LSM6DSM_OUT_XYZ_SIZE];
	uint8_t xyz_reg;
	int ret, i, range, tmp = 0;
	struct lsm6dsm_data *data = s->drv_data;

	ret = is_data_ready(s, &tmp);
	if (ret != EC_SUCCESS)
		return ret;

	/*
	 * If sensor data is not ready, return the previous read data.
	 * Note: return success so that motion senor task can read again
	 * to get the latest updated sensor data quickly.
	 */
	if (!tmp) {
		if (v != s->raw_xyz)
			memcpy(v, s->raw_xyz, sizeof(s->raw_xyz));
		return EC_SUCCESS;
	}

	xyz_reg = get_xyz_reg(s->type);

	/* Read data bytes starting at xyz_reg */
	i2c_lock(s->port, 1);
	ret = i2c_xfer(s->port, s->addr, &xyz_reg, 1, raw,
		       LSM6DSM_OUT_XYZ_SIZE, I2C_XFER_SINGLE);
	i2c_lock(s->port, 0);

	if (ret != EC_SUCCESS) {
		CPRINTF("[%T %s type:0x%X RD XYZ Error]",
			s->name, s->type);
		return ret;
	}

	for (i = X; i <= Z; i++) {
		v[i] = ((int16_t)((raw[i * 2 + 1] << 8) | raw[i * 2]));
		/* On range we seved gain related to FS */
		v[i] = v[i] * data->base.range;
	}

	/* Apply rotation matrix */
	rotate(v, *s->rot_standard_ref, v);

	/* Apply offset in the device coordinates */
	range = get_range(s);
	for (i = X; i <= Z; i++)
		v[i] += (data->offset[i] << 5) / range;

	return EC_SUCCESS;
}

static int init(const struct motion_sensor_t *s)
{
	int ret = 0, tmp;

	ret = raw_read8(s->port, s->addr, LSM6DSM_WHO_AM_I_REG, &tmp);
	if (ret != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	if (tmp != LSM6DSM_WHO_AM_I)
		return EC_ERROR_ACCESS_DENIED;

	/*
	 * This sensor can be powered through an EC reboot, so the state of
	 * the sensor is unknown here so reset it
	 * lsm6dsm supports both accel & gyro features
	 * Board will see two virtual sensor devices: accel & gyro
	 * Requirement: Accel need be init before gyro
	 */
	if (s->type == MOTIONSENSE_TYPE_ACCEL) {
		mutex_lock(s->mutex);

		/* Software reset */
		ret = write_data_with_mask(s, LSM6DSM_RESET_ADDR,
					   LSM6DSM_RESET_MASK, LSM6DSM_EN_BIT);
		if (ret != EC_SUCCESS)
			goto err_unlock;

		/* Output data not updated until have been read */
		ret = write_data_with_mask(s, LSM6DSM_BDU_ADDR,
					   LSM6DSM_BDU_MASK, LSM6DSM_EN_BIT);
		if (ret != EC_SUCCESS)
			goto err_unlock;

		mutex_unlock(s->mutex);
	}

	ret = set_range(s, s->default_range, 1);

	CPRINTF("[%T %s: MS Done Init type:0x%X range:%d]\n",
		s->name, s->type, get_range(s));
	return ret;

err_unlock:
	mutex_unlock(s->mutex);

	return EC_ERROR_UNKNOWN;
}

const struct accelgyro_drv lsm6dsm_drv = {
	.init = init,
	.read = read,
	.set_range = set_range,
	.get_range = get_range,
	.set_resolution = set_resolution,
	.get_resolution = get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = get_data_rate,
	.set_offset = set_offset,
	.get_offset = get_offset,
	.perform_calib = NULL,
};
