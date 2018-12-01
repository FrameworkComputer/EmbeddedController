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
#include "task.h"

#ifdef CONFIG_MAG_LSM6DSM_LIS2MDL
#ifndef CONFIG_SENSORHUB_LSM6DSM
#error "Need Sensor Hub LSM6DSM support"
#endif
#endif

void lis2mdl_normalize(const struct motion_sensor_t *s,
		       intv3_t v,
		       uint8_t *raw)
{
	struct mag_cal_t *cal = LIS2MDL_CAL(s);
	int i;
#ifdef CONFIG_MAG_BMI160_LIS2MDL
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

#ifdef CONFIG_LSM6DSM_SEC_I2C
	struct mag_cal_t *cal = LIS2MDL_CAL(s);
#endif

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
#ifdef CONFIG_LSM6DSM_SEC_I2C
	struct mag_cal_t *cal = LIS2MDL_CAL(s);
#endif
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
			CONFIG_ACCELGYRO_SEC_ADDR,
			LIS2MDL_WHO_AM_I_REG, LIS2MDL_WHO_AM_I,
			LIS2MDL_CFG_REG_A_ADDR, LIS2MDL_SW_RESET);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	ret = sensorhub_config_ext_reg(
			LSM6DSM_MAIN_SENSOR(s),
			CONFIG_ACCELGYRO_SEC_ADDR,
			LIS2MDL_CFG_REG_A_ADDR,
			LIS2MDL_ODR_100HZ | LIS2MDL_CONT_MODE);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	ret = sensorhub_config_slv0_read(
			LSM6DSM_MAIN_SENSOR(s),
			CONFIG_ACCELGYRO_SEC_ADDR,
			LIS2MDL_OUT_REG, OUT_XYZ_SIZE);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	mutex_unlock(s->mutex);
	init_mag_cal(cal);
	cal->radius = 0.0f;
	data->resol = LIS2DSL_RESOLUTION;
	return sensor_init_done(s);

err_unlock:
	mutex_unlock(s->mutex);
	return ret;
}
#endif  /* CONFIG_MAG_LSM6DSM_LIS2MDL */

const struct accelgyro_drv lis2mdl_drv = {
#ifdef CONFIG_MAG_LSM6DSM_LIS2MDL
	.init = lis2mdl_thru_lsm6dsm_init,
	.read = lis2mdl_thru_lsm6dsm_read,
	.set_data_rate = lsm6dsm_set_data_rate,
#endif
	.set_range = set_range,
	.get_range = get_range,
	.get_resolution = st_get_resolution,
	.get_data_rate = st_get_data_rate,
	.set_offset = set_offset,
	.get_offset = get_offset,
};
