/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * LIS2MDL magnetometer module for Chrome EC.
 * This driver supports LIS2MDL magnetometer in cascade with LSM6DSx (x stands
 * for L or M) accel/gyro module.
 */

#include "common.h"
#include "driver/mag_lis2mdl.h"
#include "driver/sensorhub_lsm6dsm.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "driver/stm_mems_common.h"
#include "hwtimer.h"
#include "mag_cal.h"
#include "task.h"

#ifdef CONFIG_MAG_LSM6DSM_LIS2MDL
#ifndef CONFIG_SENSORHUB_LSM6DSM
#error "Need Sensor Hub LSM6DSM support"
#endif
#endif

#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ## args)

void lis2mdl_normalize(const struct motion_sensor_t *s,
		       intv3_t v,
		       uint8_t *raw)
{
	struct mag_cal_t *cal = LIS2MDL_CAL(s);
	int i;

#ifdef CONFIG_MAG_BMI_LIS2MDL
	struct lis2mdl_private_data *private = LIS2MDL_DATA(s);
	intv3_t hn1;

	hn1[X] = ((int16_t)((raw[1] << 8) | raw[0]));
	hn1[Y] = ((int16_t)((raw[3] << 8) | raw[2]));
	hn1[Z] = ((int16_t)((raw[5] << 8) | raw[4]));

	/* Only when LIS2MDL is in forced mode */
	if (private->hn_valid) {
		for (i = X; i <= Z; i++)
			v[i] = (hn1[i] + private->hn[i]) / 2;
		memcpy(private->hn, hn1, sizeof(intv3_t));
	} else {
		private->hn_valid = 1;
		memcpy(v, hn1, sizeof(intv3_t));
	}
#else
	v[X] = ((int16_t)((raw[1] << 8) | raw[0]));
	v[Y] = ((int16_t)((raw[3] << 8) | raw[2]));
	v[Z] = ((int16_t)((raw[5] << 8) | raw[4]));
#endif
	for (i = X; i <= Z; i++)
		v[i] = LIS2MDL_RATIO(v[i]);

	if (IS_ENABLED(CONFIG_MAG_CALIBRATE))
		mag_cal_update(cal, v);

	v[X] += cal->bias[X];
	v[Y] += cal->bias[Y];
	v[Z] += cal->bias[Z];
}

static int set_range(const struct motion_sensor_t *s, int range, int rnd)
{
	struct stprivate_data *data = s->drv_data;

	/* Range is fixed by hardware */
	if (range != s->default_range)
		return EC_ERROR_INVAL;

	data->base.range = range;
	return EC_SUCCESS;
}

static int get_range(const struct motion_sensor_t *s)
{
	struct stprivate_data *data = s->drv_data;

	return data->base.range;
}

/**
 * set_offset - Set data offset
 * @s: Motion sensor pointer
 * @offset: offset vector
 * @temp: Temp
 */
static int set_offset(const struct motion_sensor_t *s,
		      const int16_t *offset, int16_t temp)
{
	struct mag_cal_t *cal = LIS2MDL_CAL(s);

	cal->bias[X] = offset[X];
	cal->bias[Y] = offset[Y];
	cal->bias[Z] = offset[Z];
	rotate_inv(cal->bias, *s->rot_standard_ref, cal->bias);
	return EC_SUCCESS;
}

/**
 * get_offset - Get data offset
 * @s: Motion sensor pointer
 * @offset: offset vector
 * @temp: Temp
 */
static int get_offset(const struct motion_sensor_t *s,
		      int16_t *offset, int16_t *temp)
{
	struct mag_cal_t *cal = LIS2MDL_CAL(s);
	intv3_t offset_int;

	rotate(cal->bias, *s->rot_standard_ref, offset_int);
	offset[X] = offset_int[X];
	offset[Y] = offset_int[Y];
	offset[Z] = offset_int[Z];
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

#ifdef CONFIG_MAG_LSM6DSM_LIS2MDL
int lis2mdl_thru_lsm6dsm_read(const struct motion_sensor_t *s, intv3_t v)
{
	int ret;
	uint8_t raw[OUT_XYZ_SIZE];
	/*
	 * This is mostly for debugging, read happens through LSM6DSM/BMI160
	 * FIFO.
	 */
	mutex_lock(s->mutex);
	ret = sensorhub_slv0_data_read(LSM6DSM_MAIN_SENSOR(s), raw);
	mutex_unlock(s->mutex);
	lis2mdl_normalize(s, v, raw);
	rotate(v, *s->rot_standard_ref, v);
	return ret;
}

int lis2mdl_thru_lsm6dsm_init(const struct motion_sensor_t *s)
{
	int ret = EC_ERROR_UNIMPLEMENTED;
	struct mag_cal_t *cal = LIS2MDL_CAL(s);
	struct stprivate_data *data = s->drv_data;

	mutex_lock(s->mutex);
	/* Magnetometer in cascade mode */
	ret = sensorhub_check_and_rst(
			LSM6DSM_MAIN_SENSOR(s),
			CONFIG_ACCELGYRO_SEC_ADDR_FLAGS,
			LIS2MDL_WHO_AM_I_REG, LIS2MDL_WHO_AM_I,
			LIS2MDL_CFG_REG_A_ADDR, LIS2MDL_FLAG_SW_RESET);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	ret = sensorhub_config_ext_reg(
			LSM6DSM_MAIN_SENSOR(s),
			CONFIG_ACCELGYRO_SEC_ADDR_FLAGS,
			LIS2MDL_CFG_REG_A_ADDR,
			LIS2MDL_ODR_50HZ | LIS2MDL_MODE_CONT);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	ret = sensorhub_config_slv0_read(
			LSM6DSM_MAIN_SENSOR(s),
			CONFIG_ACCELGYRO_SEC_ADDR_FLAGS,
			LIS2MDL_OUT_REG, OUT_XYZ_SIZE);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	mutex_unlock(s->mutex);
	if (IS_ENABLED(CONFIG_MAG_CALIBRATE)) {
		init_mag_cal(cal);
		cal->radius = 0.0f;
	} else {
		memset(cal, 0, sizeof(*cal));
	}
	data->resol = LIS2DSL_RESOLUTION;
	return sensor_init_done(s);

err_unlock:
	mutex_unlock(s->mutex);
	return ret;
}

#else /* END: CONFIG_MAG_LSM6DSM_LIS2MDL */

/**
 * Checks whether or not data is ready. If the check succeeds EC_SUCCESS will be
 * returned and the ready target written with the axes that are available, see:
 * <ul>
 *   <li>LIS2MDL_X_DIRTY</li>
 *   <li>LIS2MDL_Y_DIRTY</li>
 *   <li>LIS2MDL_Z_DIRTY</li>
 *   <li>LIS2MDL_XYZ_DIRTY</li>
 * </ul>
 *
 * @param s Motion sensor pointer
 * @param[out] ready Writeback pointer to store the result.
 * @return EC_SUCCESS when the status register was read successfully.
 */
static int lis2mdl_is_data_ready(const struct motion_sensor_t *s, int *ready)
{
	int ret, tmp;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags,
			   LIS2MDL_STATUS_REG, &tmp);
	if (ret != EC_SUCCESS) {
		*ready = 0;
		return ret;
	}

	*ready = tmp & LIS2MDL_XYZ_DIRTY_MASK;
	return EC_SUCCESS;

}

/**
 * Read the most recent data from the sensor. If no new data is available,
 * simply return the last available values.
 *
 * @param s Motion sensor pointer
 * @param v A vector of 3 ints for x, y, z values.
 * @return EC_SUCCESS when the values were read successfully or no new data was
 * available.
 */
int lis2mdl_read(const struct motion_sensor_t *s, intv3_t v)
{
	int ret = EC_SUCCESS, ready = 0;
	uint8_t raw[OUT_XYZ_SIZE];

	ret = lis2mdl_is_data_ready(s, &ready);
	if (ret != EC_SUCCESS)
		return ret;

	/*
	 * If sensor data is not ready, return the previous read data.
	 * Note: return success so that the motion sensor task can read again to
	 * get the latest updated sensor data quickly.
	 */
	if (!ready) {
		if (v != s->raw_xyz)
			memcpy(v, s->raw_xyz, sizeof(intv3_t));
		return ret;
	}

	mutex_lock(s->mutex);
	ret = st_raw_read_n(s->port, s->i2c_spi_addr_flags,
			    LIS2MDL_OUT_REG, raw, OUT_XYZ_SIZE);
	mutex_unlock(s->mutex);
	if (ret == EC_SUCCESS) {
		lis2mdl_normalize(s, v, raw);
		rotate(v, *s->rot_standard_ref, v);
	}
	return ret;
}

/**
 * Initialize the sensor. This function will verify the who-am-I register
 */
int lis2mdl_init(const struct motion_sensor_t *s)
{
	int ret = EC_ERROR_UNKNOWN, who_am_i, count = LIS2MDL_STARTUP_MS;
	struct stprivate_data *data = s->drv_data;
	struct mag_cal_t *cal = LIS2MDL_CAL(s);

	/* Check who am I value */
	do {
		ret = st_raw_read8(s->port, LIS2MDL_ADDR_FLAGS,
				   LIS2MDL_WHO_AM_I_REG, &who_am_i);
		if (ret != EC_SUCCESS) {
			/* Make sure we wait for the chip to start up. Sleep 1ms
			 * and try again.
			 */
			udelay(10);
			count--;
		} else {
			break;
		}
	} while (count > 0);
	if (ret != EC_SUCCESS)
		return ret;
	if (who_am_i != LIS2MDL_WHO_AM_I)
		return EC_ERROR_ACCESS_DENIED;

	mutex_lock(s->mutex);

	/* Reset the sensor */
	ret = st_raw_write8(s->port, LIS2MDL_ADDR_FLAGS,
			    LIS2MDL_CFG_REG_A_ADDR,
			    LIS2MDL_FLAG_SW_RESET);
	if (ret != EC_SUCCESS)
		goto lis2mdl_init_error;

	mutex_unlock(s->mutex);

	if (ret != EC_SUCCESS)
		return ret;

	if (IS_ENABLED(CONFIG_MAG_CALIBRATE)) {
		init_mag_cal(cal);
		cal->radius = 0.0f;
	} else {
		memset(cal, 0, sizeof(*cal));
	}
	data->resol = LIS2DSL_RESOLUTION;
	return sensor_init_done(s);

lis2mdl_init_error:
	mutex_unlock(s->mutex);
	return ret;
}

/**
 * Set the data rate of the sensor. Use a rate of 0 or below to turn off the
 * magnetometer. All other values will turn on the sensor in continuous mode.
 * The rate will be set to the nearest available value:
 * <ul>
 *   <li>LIS2MDL_ODR_10HZ</li>
 *   <li>LIS2MDL_ODR_20HZ</li>
 *   <li>LIS2MDL_ODR_50HZ</li>
 * </ul>
 *
 * @param s Motion sensor pointer
 * @param rate Rate (mHz)
 * @param rnd Flag used to tell whether or not to round up (1) or down (0)
 * @return EC_SUCCESS when the rate was successfully changed.
 */
int lis2mdl_set_data_rate(const struct motion_sensor_t *s, int rate, int rnd)
{
	int ret = EC_SUCCESS, normalized_rate = 0;
	uint8_t reg_val = 0;
	struct mag_cal_t *cal = LIS2MDL_CAL(s);
	struct stprivate_data *data = s->drv_data;

	if (rate > 0) {
		if (rnd)
			/* Round up */
			reg_val = rate <= 10000 ? LIS2MDL_ODR_10HZ
				: rate <= 20000 ? LIS2MDL_ODR_20HZ
				: LIS2MDL_ODR_50HZ;
		else
			/* Round down */
			reg_val = rate < 20000 ? LIS2MDL_ODR_10HZ
				: rate < 50000 ? LIS2MDL_ODR_20HZ
				: LIS2MDL_ODR_50HZ;
	}

	normalized_rate = rate <= 0 ? 0
		: reg_val == LIS2MDL_ODR_10HZ ? 10000
		: reg_val == LIS2MDL_ODR_20HZ ? 20000
		: 50000;

	/*
	 * If no change is needed just bail. Not doing so will require a reset
	 * of the chip which only leads to re-calibration and lost samples.
	 */
	if (normalized_rate == data->base.odr)
		return ret;

	if (IS_ENABLED(CONFIG_MAG_CALIBRATE))
		init_mag_cal(cal);

	if (normalized_rate > 0)
		cal->batch_size = MAX(
				MAG_CAL_MIN_BATCH_SIZE,
				(normalized_rate * 1000) /
					MAG_CAL_MIN_BATCH_WINDOW_US);
	else
		cal->batch_size = 0;

	mutex_lock(s->mutex);
	if (rate <= 0) {
		ret = st_raw_write8(s->port, LIS2MDL_ADDR_FLAGS,
				    LIS2MDL_CFG_REG_A_ADDR,
				    LIS2MDL_FLAG_SW_RESET);
	} else {
		/* Add continuous and temp compensation flags */
		reg_val |= LIS2MDL_MODE_CONT | LIS2MDL_FLAG_TEMP_COMPENSATION;
		ret = st_raw_write8(s->port, LIS2MDL_ADDR_FLAGS,
				    LIS2MDL_CFG_REG_A_ADDR, reg_val);
	}

	mutex_unlock(s->mutex);

	if (ret == EC_SUCCESS)
		data->base.odr = normalized_rate;

	return ret;
}

#endif /* CONFIG_MAG_LIS2MDL */

const struct accelgyro_drv lis2mdl_drv = {
#ifdef CONFIG_MAG_LSM6DSM_LIS2MDL
	.init = lis2mdl_thru_lsm6dsm_init,
	.read = lis2mdl_thru_lsm6dsm_read,
	.set_data_rate = lsm6dsm_set_data_rate,
#else /* CONFIG_MAG_LSM6DSM_LIS2MDL */
	.init = lis2mdl_init,
	.read = lis2mdl_read,
	.set_data_rate = lis2mdl_set_data_rate,
#endif /* !CONFIG_MAG_LSM6DSM_LIS2MDL */
	.set_range = set_range,
	.get_range = get_range,
	.get_data_rate = st_get_data_rate,
	.get_resolution = st_get_resolution,
	.set_offset = set_offset,
	.get_offset = get_offset,
};
