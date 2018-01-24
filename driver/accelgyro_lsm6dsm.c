/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * LSM6DSx (x is L or M) accelerometer and gyro module for Chrome EC
 * 3D digital accelerometer & 3D digital gyroscope
 * This driver supports both devices LSM6DSM and LSM6DSL
 */

#include "driver/accelgyro_lsm6dsm.h"
#include "hooks.h"
#include "math_util.h"
#include "task.h"

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ## args)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ## args)

/**
 * @return output base register for sensor
 */
static inline int get_xyz_reg(enum motionsensor_type type)
{
	return LSM6DSM_ACCEL_OUT_X_L_ADDR -
		(LSM6DSM_ACCEL_OUT_X_L_ADDR - LSM6DSM_GYRO_OUT_X_L_ADDR) * type;
}

/**
 * set_range - set full scale range
 * @s: Motion sensor pointer
 * @range: Range
 * @rnd: Round up/down flag
 * Note: Range is sensitivity/gain for speed purpose
 */
static int set_range(const struct motion_sensor_t *s, int range, int rnd)
{
	int err;
	uint8_t ctrl_reg, reg_val;
	struct stprivate_data *data = s->drv_data;
	int newrange = range;

	ctrl_reg = LSM6DSM_RANGE_REG(s->type);
	if (s->type == MOTIONSENSE_TYPE_ACCEL) {
		/* Adjust and check rounded value for acc. */
		if (rnd && (newrange < LSM6DSM_ACCEL_NORMALIZE_FS(newrange)))
			newrange *= 2;

		if (newrange > LSM6DSM_ACCEL_FS_MAX_VAL)
			newrange = LSM6DSM_ACCEL_FS_MAX_VAL;

		reg_val = LSM6DSM_ACCEL_FS_REG(newrange);
	} else {
		/* Adjust and check rounded value for gyro. */
		if (rnd && (newrange < LSM6DSM_GYRO_NORMALIZE_FS(newrange)))
			newrange *= 2;

		if (newrange > LSM6DSM_GYRO_FS_MAX_VAL)
			newrange = LSM6DSM_GYRO_FS_MAX_VAL;

		reg_val = LSM6DSM_GYRO_FS_REG(newrange);
	}

	mutex_lock(s->mutex);
	err = st_write_data_with_mask(s, ctrl_reg, LSM6DSM_RANGE_MASK, reg_val);
	if (err == EC_SUCCESS)
		/* Save internally gain for speed optimization. */
		data->base.range = (s->type == MOTIONSENSE_TYPE_ACCEL ?
				    newrange :
				    LSM6DSM_GYRO_FS_GAIN(newrange));
	mutex_unlock(s->mutex);

	return EC_SUCCESS;
}

/**
 * get_range - get full scale range
 * @s: Motion sensor pointer
 *
 * For mag range is fixed to LIS2MDL_RANGE by hardware
 */
static int get_range(const struct motion_sensor_t *s)
{
	struct stprivate_data *data = s->drv_data;

	if (s->type == MOTIONSENSE_TYPE_ACCEL)
		return data->base.range;
	return LSM6DSM_GYRO_GAIN_FS(data->base.range);
}

/**
 * set_data_rate
 * @s: Motion sensor pointer
 * @range: Rate (mHz)
 * @rnd: Round up/down flag
 *
 * For mag in cascade with lsm6dsm/l we use acc trigger and FIFO decimator
 */
static int set_data_rate(const struct motion_sensor_t *s, int rate, int rnd)
{
	int ret, normalized_rate = LSM6DSM_ODR_MIN_VAL;
	struct stprivate_data *data = s->drv_data;
	uint8_t ctrl_reg, reg_val;

	ctrl_reg = LSM6DSM_ODR_REG(s->type);

	if (rate == 0) {
		/* Power off acc or gyro. */
		mutex_lock(s->mutex);

		ret = st_write_data_with_mask(s, ctrl_reg, LSM6DSM_ODR_MASK,
					      LSM6DSM_ODR_0HZ_VAL);
		if (ret == EC_SUCCESS)
			data->base.odr = LSM6DSM_ODR_0HZ_VAL;

		mutex_unlock(s->mutex);

		return ret;
	}

	reg_val = LSM6DSM_ODR_TO_REG(rate);
	normalized_rate = LSM6DSM_ODR_TO_NORMALIZE(rate);

	if (rnd && (normalized_rate < rate)) {
		reg_val++;
		normalized_rate <<= 1;
	}

	/* Adjust rounded value for acc and gyro because ODR are shared. */
	if (reg_val > LSM6DSM_ODR_416HZ_VAL) {
		reg_val = LSM6DSM_ODR_416HZ_VAL;
		normalized_rate = LSM6DSM_ODR_MAX_VAL;
	} else if (reg_val < LSM6DSM_ODR_13HZ_VAL) {
		reg_val = LSM6DSM_ODR_13HZ_VAL;
		normalized_rate = LSM6DSM_ODR_MIN_VAL;
	}

	mutex_lock(s->mutex);
	ret = st_write_data_with_mask(s, ctrl_reg, LSM6DSM_ODR_MASK, reg_val);
	if (ret == EC_SUCCESS)
		data->base.odr = normalized_rate;

	mutex_unlock(s->mutex);

	return ret;
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
 * sensor is ready (minimize I2C access).
 */
static int read(const struct motion_sensor_t *s, vector_3_t v)
{
	uint8_t raw[OUT_XYZ_SIZE];
	uint8_t xyz_reg;
	int ret, tmp = 0;

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

	/* Read data bytes starting at xyz_reg. */
	ret = st_raw_read_n_noinc(s->port, s->addr, xyz_reg, raw, OUT_XYZ_SIZE);
	if (ret != EC_SUCCESS)
		return ret;

	/* Apply precision, sensitivity and rotation vector. */
	st_normalize(s, v, raw);

	return EC_SUCCESS;
}

static int init(const struct motion_sensor_t *s)
{
	int ret = 0, tmp;
	struct stprivate_data *data = s->drv_data;

	ret = raw_read8(s->port, s->addr, LSM6DSM_WHO_AM_I_REG, &tmp);
	if (ret != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	if (tmp != LSM6DSM_WHO_AM_I)
		return EC_ERROR_ACCESS_DENIED;

	/*
	 * This sensor can be powered through an EC reboot, so the state of the
	 * sensor is unknown here so reset it
	 * LSM6DSM/L supports both accel & gyro features
	 * Board will see two virtual sensor devices: accel & gyro
	 * Requirement: Accel need be init before gyro and mag
	 */
	if (s->type == MOTIONSENSE_TYPE_ACCEL) {
		mutex_lock(s->mutex);

		/* Software reset. */
		ret = st_write_data_with_mask(s,
				LSM6DSM_RESET_ADDR,
				LSM6DSM_RESET_MASK,
				LSM6DSM_EN_BIT);
		if (ret != EC_SUCCESS)
			goto err_unlock;

		/* Output data not updated until have been read. */
		ret = st_write_data_with_mask(s,
				LSM6DSM_BDU_ADDR,
				LSM6DSM_BDU_MASK,
				LSM6DSM_EN_BIT);
		if (ret != EC_SUCCESS)
			goto err_unlock;

		mutex_unlock(s->mutex);
	}

	/* Set default resolution common to acc and gyro. */
	data->resol = LSM6DSM_RESOLUTION;
	return sensor_init_done(s);

err_unlock:
	mutex_unlock(s->mutex);

	return ret;
}

const struct accelgyro_drv lsm6dsm_drv = {
	.init = init,
	.read = read,
	.set_range = set_range,
	.get_range = get_range,
	.get_resolution = st_get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = st_get_data_rate,
	.set_offset = st_set_offset,
	.get_offset = st_get_offset,
};
