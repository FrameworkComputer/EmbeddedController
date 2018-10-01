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
#include "driver/stm_mems_common.h"
#include "task.h"

#ifndef CONFIG_SENSORHUB_LSM6DSM
#error "Need Sensor Hub LSM6DSM support"
#endif

static int irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	if (!s->parent) {
		/* Sensor is not supported in direct connection/slave mode */
		return EC_ERROR_UNIMPLEMENTED;
	}
	/*
	 * Magnetometer in cascade mode. The main sensor should take
	 * care of interrupt situation.
	 */
	return EC_SUCCESS;
}

static int set_data_rate(const struct motion_sensor_t *s, int rate, int rnd)
{
	int ret = EC_ERROR_UNIMPLEMENTED;
	/*
	 * Since 'stprivate_data a_data;' is the first member of lsm6dsm_data,
	 * the address of lsm6dsm_data is the same as a_data's. Using this
	 * fact, we can do the following conversion. This conversion is equal
	 * to:
	 *     struct lsm6dsm_data *lsm_data = s->drv_data;
	 *     struct stprivate_data *data = &lsm_data->a_data;
	 */
	struct stprivate_data *data = s->drv_data;

	if (!s->parent)
		return ret;

	mutex_lock(s->mutex);
	ret = sensorhub_set_ext_data_rate(s->parent, rate, rnd,
						&data->base.odr);
	mutex_unlock(s->mutex);

	return ret;
}

static int set_range(const struct motion_sensor_t *s, int range, int rnd)
{
	struct stprivate_data *data = s->drv_data;

	if (range != LIS2MDL_RANGE)
		return EC_ERROR_INVAL;

	/* Range is fixed to LIS2MDL_RANGE by hardware */
	data->base.range = LIS2MDL_RANGE;
	return EC_SUCCESS;
}

static int get_range(const struct motion_sensor_t *s)
{
	struct stprivate_data *data = s->drv_data;

	return data->base.range;
}

static int read(const struct motion_sensor_t *s, intv3_t v)
{
	int ret = EC_ERROR_UNIMPLEMENTED;

	if (!s->parent)
		return ret;

	mutex_lock(s->mutex);
	ret = sensorhub_slv0_data_read(s->parent, v);
	mutex_unlock(s->mutex);

	return ret;
}

static int init(const struct motion_sensor_t *s)
{
	int ret = EC_ERROR_UNIMPLEMENTED;
	struct stprivate_data *data = s->drv_data;

	if (!s->parent)
		return ret;

	mutex_lock(s->mutex);
	/* Magnetometer in cascade mode */
	ret = sensorhub_check_and_rst(s->parent, s->addr,
			LIS2MDL_WHO_AM_I_REG, LIS2MDL_WHO_AM_I,
			LIS2MDL_CFG_REG_A_ADDR, LIS2MDL_SW_RESET);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	ret = sensorhub_config_ext_reg(s->parent, s->addr,
			LIS2MDL_CFG_REG_A_ADDR,
			LIS2MDL_ODR_100HZ | LIS2MDL_CONT_MODE);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	ret = sensorhub_config_slv0_read(s->parent, s->addr,
				LIS2MDL_OUT_REG, OUT_XYZ_SIZE);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	/* Set default resolution to 16 bit */
	data->resol = LIS2MDL_RESOLUTION;
	/* Range is fixed to LIS2MDL_RANGE by hardware */
	data->base.range = LIS2MDL_RANGE;
	mutex_unlock(s->mutex);

	return sensor_init_done(s);

err_unlock:
	mutex_unlock(s->mutex);
	return ret;
}

const struct accelgyro_drv lis2mdl_drv = {
	.init = init,
	.read = read,
	.set_range = set_range,
	.get_range = get_range,
	.get_resolution = st_get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = st_get_data_rate,
	.set_offset = st_set_offset,
	.get_offset = st_get_offset,
	.irq_handler = irq_handler,
};
