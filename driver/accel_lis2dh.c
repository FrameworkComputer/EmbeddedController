/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * LIS2DH/LIS2DH12 accelerometer module for Chrome EC 3D digital accelerometer
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
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ## args)

#ifdef CONFIG_ACCEL_FIFO
/**
 * enable_fifo - Enable/Disable FIFO in LIS2DH
 * @s: Motion sensor pointer
 * @mode: fifo_modes
 * @en_dis: LIS2DH_EN_BIT/LIS2DH_DIS_BIT
 */
static int enable_fifo(const struct motion_sensor_t *s, int mode, int en_dis)
{
	int ret;

	ret = st_write_data_with_mask(s, LIS2DH_FIFO_CTRL_REG,
				      LIS2DH_FIFO_MODE_MASK, mode);
	if (ret != EC_SUCCESS)
		return ret;

	ret = st_write_data_with_mask(s, LIS2DH_CTRL5_ADDR, LIS2DH_FIFO_EN_MASK,
				      en_dis);

	return ret;
}
#endif /* CONFIG_ACCEL_FIFO */

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

	/* Lock accel resource to prevent another task from attempting
	 * to write accel parameters until we are done */
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

	return LIS2DH_GAIN_TO_FS(data->base.range);
}

static int set_data_rate(const struct motion_sensor_t *s, int rate, int rnd)
{
	int ret, normalized_rate;
	struct stprivate_data *data = s->drv_data;
	uint8_t reg_val;

	mutex_lock(s->mutex);

#ifdef CONFIG_ACCEL_FIFO
	/* FIFO stop collecting events. Restart FIFO in Bypass mode */
	ret = enable_fifo(s, LIS2DH_FIFO_BYPASS_MODE, LIS2DH_DIS_BIT);
	if (ret != EC_SUCCESS)
		goto unlock_rate;
#endif /* CONFIG_ACCEL_FIFO */

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

	/* Adjust rounded value */
	if (reg_val > LIS2DH_ODR_400HZ_VAL) {
		reg_val = LIS2DH_ODR_400HZ_VAL;
		normalized_rate = LIS2DH_ODR_MAX_VAL;
	} else if (reg_val < LIS2DH_ODR_1HZ_VAL) {
		reg_val = LIS2DH_ODR_1HZ_VAL;
		normalized_rate = LIS2DH_ODR_MIN_VAL;
	}

	/*
	 * Lock accel resource to prevent another task from attempting
	 * to write accel parameters until we are done
	 */
	ret = st_write_data_with_mask(s, LIS2DH_CTRL1_ADDR, LIS2DH_ACC_ODR_MASK,
			reg_val);
	if (ret == EC_SUCCESS)
		data->base.odr = normalized_rate;

#ifdef CONFIG_ACCEL_FIFO
	/* FIFO restart collecting events */
	ret = enable_fifo(s, LIS2DH_FIFO_STREAM_MODE, LIS2DH_EN_BIT);
#endif /* CONFIG_ACCEL_FIFO */

unlock_rate:
	mutex_unlock(s->mutex);
	return ret;
}

#ifdef CONFIG_ACCEL_FIFO
/*
 * Load data from internal sensor FIFO (deep 32 byte)
 */
static int load_fifo(struct motion_sensor_t *s)
{
	int ret, tmp, nsamples, i;
	struct ec_response_motion_sensor_data vect;
	int done = 0;
	int *axis = s->raw_xyz;
	uint8_t fifo[FIFO_READ_LEN];

	/* Try to Empty FIFO */
	do {
		/* Read samples number in status register */
		ret = raw_read8(s->port, s->addr, LIS2DH_FIFO_SRC_REG, &tmp);
		if (ret != EC_SUCCESS)
			return ret;

		/* Check FIFO empty flag */
		if (tmp & LIS2DH_FIFO_EMPTY_FLAG)
			return EC_SUCCESS;

		nsamples = (tmp & LIS2DH_FIFO_UNREAD_MASK) * OUT_XYZ_SIZE;

		/* Limit FIFO read data to burst of FIFO_READ_LEN size because
		 * read operatios in under i2c mutex lock */
		if (nsamples > FIFO_READ_LEN)
			nsamples = FIFO_READ_LEN;
		else
			done = 1;

		ret = st_raw_read_n(s->port, s->addr, LIS2DH_OUT_X_L_ADDR, fifo,
				 nsamples);
		if (ret != EC_SUCCESS)
			return ret;

		for (i = 0; i < nsamples; i += OUT_XYZ_SIZE) {
			/* Apply precision, sensitivity and rotation vector */
			st_normalize(s, axis, &fifo[i]);

			/* Fill vector array */
			vect.data[0] = axis[0];
			vect.data[1] = axis[1];
			vect.data[2] = axis[2];
			vect.flags = 0;
			vect.sensor_num = 0;
			motion_sense_fifo_add_data(&vect, s, 3,
						   __hw_clock_source_read());
			/*
			 * TODO: get time at a more accurate spot.
			 * Like in lis2dh_interrupt
			 */
		}
	} while(!done);

	return EC_SUCCESS;
}
#endif  /* CONFIG_ACCEL_FIFO */

#ifdef CONFIG_ACCEL_INTERRUPTS
static int config_interrupt(const struct motion_sensor_t *s)
{
	int ret;

#ifdef CONFIG_ACCEL_FIFO_THRES
	/* configure FIFO watermark level */
	ret = st_write_data_with_mask(s, LIS2DH_FIFO_CTRL_REG,
				   LIS2DH_FIFO_THR_MASK,
				   CONFIG_ACCEL_FIFO_THRES);
	if (ret != EC_SUCCESS)
		return ret;
	/* enable interrupt on FIFO watermask and route to int1 */
	ret = st_write_data_with_mask(s, LIS2DH_CTRL3_ADDR,
				   LIS2DH_FIFO_WTM_INT_MASK, 1);
#endif /* CONFIG_ACCEL_FIFO */

	return ret;
}

/**
 * lis2dh_interrupt - interrupt from int1/2 pin of sensor
 */
void lis2dh_interrupt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_MOTIONSENSE,
		       CONFIG_ACCEL_LIS2DH_INT_EVENT, 0);
}

/**
 * irq_handler - bottom half of the interrupt stack.
 */
static int irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	int interrupt;

	if ((s->type != MOTIONSENSE_TYPE_ACCEL) ||
	    (!(*event & CONFIG_ACCEL_LIS2DH_INT_EVENT))) {
		return EC_ERROR_NOT_HANDLED;
	}

	/* read interrupt status register to reset source */
	raw_read8(s->port, s->addr, LIS2DH_INT1_SRC_REG, &interrupt);

#ifdef CONFIG_GESTURE_SENSOR_BATTERY_TAP
	*event |= CONFIG_GESTURE_TAP_EVENT;
#endif
#ifdef CONFIG_GESTURE_SIGMO
	*event |= CONFIG_GESTURE_SIGMO_EVENT;
#endif
	/*
	 * No need to read the FIFO here, motion sense task is
	 * doing it on every interrupt.
	 */
	return EC_SUCCESS;
}
#endif  /* CONFIG_ACCEL_INTERRUPTS */

static int is_data_ready(const struct motion_sensor_t *s, int *ready)
{
	int ret, tmp;

	ret = raw_read8(s->port, s->addr, LIS2DH_STATUS_REG, &tmp);
	if (ret != EC_SUCCESS) {
		CPRINTF("[%T %s type:0x%X RS Error]", s->name, s->type);
		return ret;
	}

	*ready = (LIS2DH_STS_XLDA_UP == (tmp & LIS2DH_STS_XLDA_UP));

	return EC_SUCCESS;
}

static int read(const struct motion_sensor_t *s, vector_3_t v)
{
	uint8_t raw[OUT_XYZ_SIZE];
	int ret, i, tmp = 0;
	struct stprivate_data *data = s->drv_data;

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
	ret = st_raw_read_n(s->port, s->addr, LIS2DH_OUT_X_L_ADDR, raw,
			 OUT_XYZ_SIZE);
	if (ret != EC_SUCCESS) {
		CPRINTF("[%T %s type:0x%X RD XYZ Error]",
			s->name, s->type);
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

	ret = raw_read8(s->port, s->addr, LIS2DH_WHO_AM_I_REG, &tmp);
	if (ret != EC_SUCCESS)
		return ret;

	if (tmp != LIS2DH_WHO_AM_I)
		return EC_ERROR_ACCESS_DENIED;

	mutex_lock(s->mutex);
	/* Device can be re-initialized after a reboot so any control
	 * register must be restored to it's default
	 */
	/* Enable all accel axes data and clear old settings */
	ret = raw_write8(s->port, s->addr, LIS2DH_CTRL1_ADDR,
			 LIS2DH_ENABLE_ALL_AXES);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	ret = raw_write8(s->port, s->addr, LIS2DH_CTRL2_ADDR,
			 LIS2DH_CTRL2_RESET_VAL);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	ret = raw_write8(s->port, s->addr, LIS2DH_CTRL3_ADDR,
			 LIS2DH_CTRL3_RESET_VAL);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	/* Enable BDU */
	ret = raw_write8(s->port, s->addr, LIS2DH_CTRL4_ADDR,
			 LIS2DH_BDU_MASK);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	ret = raw_write8(s->port, s->addr, LIS2DH_CTRL5_ADDR,
			 LIS2DH_CTRL5_RESET_VAL);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	ret = raw_write8(s->port, s->addr, LIS2DH_CTRL6_ADDR,
			 LIS2DH_CTRL6_RESET_VAL);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	mutex_unlock(s->mutex);

	/* Set default resolution */
	data->resol = LIS2DH_RESOLUTION;

#ifdef CONFIG_ACCEL_INTERRUPTS
	ret = config_interrupt(s);
	if (ret != EC_SUCCESS)
		return ret;
#endif

	return sensor_init_done(s);

err_unlock:
	CPRINTF("[%T %s: MS Init type:0x%X Error]\n", s->name, s->type);
	mutex_unlock(s->mutex);

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
	.perform_calib = NULL,
#ifdef CONFIG_ACCEL_INTERRUPTS
	.irq_handler = irq_handler,
#endif /* CONFIG_ACCEL_INTERRUPTS */
};
