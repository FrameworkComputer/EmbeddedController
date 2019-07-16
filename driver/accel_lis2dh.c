/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * Accelerometer module driver for Chrome EC 3D digital accelerometers:
 * LIS2DH/LIS2DH12/LNG2DM
 */

#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "hwtimer.h"
#include "i2c.h"
#include "math_util.h"
#include "task.h"
#include "util.h"
#include "driver/accel_lis2dh.h"
#include "driver/stm_mems_common.h"

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ## args)

/**
 * set_range - set full scale range
 * @s: Motion sensor pointer
 * @range: Range
 * @rnd: Round up/down flag
 */
static int set_range(const struct motion_sensor_t *s, int range, int rnd)
{
	int err, normalized_range;
	struct stprivate_data *data = s->drv_data;
	int val;

	val = LIS2DH_FS_TO_REG(range);
	normalized_range = ST_NORMALIZE_RATE(range);

	if (rnd && (range < normalized_range))
		val++;

	/* Adjust rounded values */
	if (val > LIS2DH_FS_16G_VAL) {
		val = LIS2DH_FS_16G_VAL;
		normalized_range = 16;
	}

	if (val < LIS2DH_FS_2G_VAL) {
		val = LIS2DH_FS_2G_VAL;
		normalized_range = 2;
	}

	/*
	 * Lock accel resource to prevent another task from attempting
	 * to write accel parameters until we are done.
	 */
	mutex_lock(s->mutex);
	err = st_write_data_with_mask(s, LIS2DH_CTRL4_ADDR, LIS2DH_FS_MASK,
				      val);

	/* Save Gain in range for speed up data path */
	if (err == EC_SUCCESS)
		data->base.range = normalized_range;

	mutex_unlock(s->mutex);
	return EC_SUCCESS;
}

static int get_range(const struct motion_sensor_t *s)
{
	struct stprivate_data *data = s->drv_data;

	return data->base.range;
}

static int set_data_rate(const struct motion_sensor_t *s, int rate, int rnd)
{
	int ret, normalized_rate;
	struct stprivate_data *data = s->drv_data;
	uint8_t reg_val;

	mutex_lock(s->mutex);

	if (rate == 0) {
		/* Power Off device */
		ret = st_write_data_with_mask(
				s, LIS2DH_CTRL1_ADDR,
				LIS2DH_ACC_ODR_MASK, LIS2DH_ODR_0HZ_VAL);
		goto unlock_rate;
	}

	reg_val = LIS2DH_ODR_TO_REG(rate);
	normalized_rate = LIS2DH_ODR_TO_NORMALIZE(rate);

	if (rnd && (normalized_rate < rate)) {
		reg_val++;
		normalized_rate = LIS2DH_REG_TO_NORMALIZE(reg_val);
	}

	if (normalized_rate > LIS2DH_ODR_MAX_VAL ||
	    normalized_rate < LIS2DH_ODR_MIN_VAL)
		return EC_RES_INVALID_PARAM;

	/*
	 * Lock accel resource to prevent another task from attempting
	 * to write accel parameters until we are done
	 */
	ret = st_write_data_with_mask(s, LIS2DH_CTRL1_ADDR, LIS2DH_ACC_ODR_MASK,
			reg_val);
	if (ret == EC_SUCCESS)
		data->base.odr = normalized_rate;

unlock_rate:
	mutex_unlock(s->mutex);
	return ret;
}

static int is_data_ready(const struct motion_sensor_t *s, int *ready)
{
	int ret, tmp;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags,
			   LIS2DH_STATUS_REG, &tmp);
	if (ret != EC_SUCCESS) {
		CPRINTS("%s type:0x%X RS Error", s->name, s->type);
		return ret;
	}

	*ready = (LIS2DH_STS_XLDA_UP == (tmp & LIS2DH_STS_XLDA_UP));

	return EC_SUCCESS;
}

static int read(const struct motion_sensor_t *s, intv3_t v)
{
	uint8_t raw[OUT_XYZ_SIZE];
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

	/* Read output data bytes starting at LIS2DH_OUT_X_L_ADDR */
	ret = st_raw_read_n(s->port, s->i2c_spi_addr_flags,
			    LIS2DH_OUT_X_L_ADDR, raw, OUT_XYZ_SIZE);
	if (ret != EC_SUCCESS) {
		CPRINTS("%s type:0x%X RD XYZ Error", s->name, s->type);
		return ret;
	}

	/* Transform from LSB to real data with rotation and gain */
	st_normalize(s, v, raw);

	return EC_SUCCESS;
}

static int init(const struct motion_sensor_t *s)
{
	int ret = 0, tmp;
	struct stprivate_data *data = s->drv_data;
	int count = 10;

	/*
	 * lis2de need 5 milliseconds to complete boot procedure after
	 * device power-up. When sensor is powered on, it can't be
	 * accessed immediately. We need wait serval loops to let sensor
	 * complete boot procedure.
	 */
	do {
		ret = st_raw_read8(s->port, s->i2c_spi_addr_flags,
				   LIS2DH_WHO_AM_I_REG, &tmp);
		if (ret != EC_SUCCESS) {
			udelay(10);
			count--;
		} else {
			break;
		}
	} while (count > 0);

	if (ret != EC_SUCCESS)
		return ret;

	if (tmp != LIS2DH_WHO_AM_I)
		return EC_ERROR_ACCESS_DENIED;

	mutex_lock(s->mutex);
	/*
	 * Device can be re-initialized after a reboot so any control
	 * register must be restored to it's default.
	 */
	/* Enable all accel axes data and clear old settings */
	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
			    LIS2DH_CTRL1_ADDR, LIS2DH_ENABLE_ALL_AXES);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
			    LIS2DH_CTRL2_ADDR, LIS2DH_CTRL2_RESET_VAL);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
			    LIS2DH_CTRL3_ADDR, LIS2DH_CTRL3_RESET_VAL);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	/* Enable BDU */
	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
			    LIS2DH_CTRL4_ADDR, LIS2DH_BDU_MASK);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
			    LIS2DH_CTRL5_ADDR, LIS2DH_CTRL5_RESET_VAL);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
			    LIS2DH_CTRL6_ADDR, LIS2DH_CTRL6_RESET_VAL);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	mutex_unlock(s->mutex);

	/* Set default resolution */
	data->resol = LIS2DH_RESOLUTION;

	return sensor_init_done(s);

err_unlock:
	mutex_unlock(s->mutex);
	CPRINTS("%s: MS Init type:0x%X Error", s->name, s->type);

	return ret;
}

const struct accelgyro_drv lis2dh_drv = {
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
