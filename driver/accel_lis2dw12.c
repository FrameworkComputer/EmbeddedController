/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * LIS2DW12 accelerometer module for Chrome EC 3D digital accelerometer.
 * For more details on LIS2DW12 device please refers to www.st.com.
 */
#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "driver/accel_lis2dw12.h"
#include "hooks.h"
#include "hwtimer.h"
#include "math_util.h"
#include "motion_sense_fifo.h"
#include "task.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ## args)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ## args)

/* Only when configured as base accel sensor, fifo and interrupt
 * are supported.
 */
#ifdef CONFIG_ACCEL_LIS2DW_AS_BASE

#ifdef CONFIG_ACCEL_FIFO
static volatile uint32_t last_interrupt_timestamp;

/**
 * lis2dw12_enable_fifo - Enable/Disable FIFO in LIS2DW12
 * @s: Motion sensor pointer
 * @mode: fifo_modes
 */
static int lis2dw12_enable_fifo(const struct motion_sensor_t *s,
				enum lis2dw12_fmode mode)
{
	return st_write_data_with_mask(s, LIS2DW12_FIFO_CTRL_ADDR,
				       LIS2DW12_FIFO_MODE_MASK, mode);
}

/**
 * Load data from internal sensor FIFO.
 * @s: Motion sensor pointer
 */
static int lis2dw12_load_fifo(struct motion_sensor_t *s, int nsamples,
			      uint32_t *last_fifo_read_ts)
{
	int ret, left, length, i;
	struct ec_response_motion_sensor_data vect;
	uint32_t interrupt_timestamp = last_interrupt_timestamp;
	int *axis = s->raw_xyz;
	uint8_t fifo[FIFO_READ_LEN];

	/* Each sample are OUT_XYZ_SIZE bytes. */
	left = nsamples * OUT_XYZ_SIZE;

	do {
		/*
		 * Limit FIFO read data to burst of FIFO_READ_LEN size because
		 * read operatios in under i2c mutex lock.
		 */
		if (left > FIFO_READ_LEN)
			length = FIFO_READ_LEN;
		else
			length = left;

		ret = st_raw_read_n(s->port, s->i2c_spi_addr_flags,
				    LIS2DW12_OUT_X_L_ADDR, fifo, length);
		*last_fifo_read_ts = __hw_clock_source_read();
		if (ret != EC_SUCCESS)
			return ret;

		for (i = 0; i < length; i += OUT_XYZ_SIZE) {
			/* Apply precision, sensitivity and rotation vector. */
			st_normalize(s, axis, &fifo[i]);

			/* Fill vector array. */
			vect.data[X] = axis[X];
			vect.data[Y] = axis[Y];
			vect.data[Z] = axis[Z];
			vect.flags = 0;
			vect.sensor_num = 0;
			motion_sense_fifo_stage_data(&vect, s, 3,
						     interrupt_timestamp);
		}
		left -= length;
	} while (left > 0);

	motion_sense_fifo_commit_data();

	return EC_SUCCESS;
}

/**
 * lis2dw12_get_fifo_samples - check for stored FIFO samples.
 */
static int lis2dw12_get_fifo_samples(struct motion_sensor_t *s, int *nsamples)
{
	int ret, tmp;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags,
			   LIS2DW12_FIFO_SAMPLES_ADDR, &tmp);
	if (ret != EC_SUCCESS)
		return ret;

	*nsamples = tmp & LIS2DW12_FIFO_DIFF_MASK;

	return EC_SUCCESS;
}

static int fifo_data_avail(struct motion_sensor_t *s)
{
	int ret, nsamples;

	if (s->flags & MOTIONSENSE_FLAG_INT_SIGNAL)
		return gpio_get_level(s->int_signal) ==
			!!(MOTIONSENSE_FLAG_INT_ACTIVE_HIGH & s->flags);

	ret = lis2dw12_get_fifo_samples(s, &nsamples);
	/* If we failed to read the FIFO size assume empty. */
	if (ret != EC_SUCCESS)
		return 0;
	return nsamples;
}
#endif  /* CONFIG_ACCEL_FIFO */

/**
 * lis2dw12_config_interrupt- Configure interrupt for supported features.
 * @s: Motion sensor pointer
 *
 * Must works with interface mutex locked
 */
static int lis2dw12_config_interrupt(const struct motion_sensor_t *s)
{
	int ret = EC_SUCCESS;

#ifdef CONFIG_ACCEL_FIFO_THRES
	/* Configure FIFO watermark level. */
	ret = st_write_data_with_mask(s, LIS2DW12_FIFO_CTRL_ADDR,
				      LIS2DW12_FIFO_THRESHOLD_MASK, 1);
	if (ret != EC_SUCCESS)
		return ret;

	/* Enable interrupt on FIFO watermask and route to int1. */
	ret = st_write_data_with_mask(s, LIS2DW12_INT1_FTH_ADDR,
				      LIS2DW12_INT1_FTH_MASK, LIS2DW12_EN_BIT);
	if (ret != EC_SUCCESS)
		return ret;
#endif /* CONFIG_ACCEL_FIFO */

#ifdef CONFIG_GESTURE_SENSOR_DOUBLE_TAP
	/*
	 * Configure D-TAP event detection on 3 axis.
	 * For more details please refer to AN5038.
	 */
	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
			    LIS2DW12_TAP_THS_X_ADDR, 0x09);
	if (ret != EC_SUCCESS)
		return ret;
	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
			    LIS2DW12_TAP_THS_Y_ADDR, 0x09);
	if (ret != EC_SUCCESS)
		return ret;
	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
			    LIS2DW12_TAP_THS_Z_ADDR, 0xE9);
	if (ret != EC_SUCCESS)
		return ret;
	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
			    LIS2DW12_INT_DUR_ADDR, 0x7F);
	if (ret != EC_SUCCESS)
		return ret;

	/* Enable D-TAP event detection. */
	ret = st_write_data_with_mask(s, LIS2DW12_WAKE_UP_THS_ADDR,
				      LIS2DW12_SINGLE_DOUBLE_TAP,
				      LIS2DW12_EN_BIT);
	if (ret != EC_SUCCESS)
		return ret;

	/*
	 * Enable D-TAP detection on int_1 pad. In any case D-TAP event
	 * can be detected only if ODR is over 200 Hz.
	 */
	ret = st_write_data_with_mask(s, LIS2DW12_INT1_TAP_ADDR,
				      LIS2DW12_INT1_DTAP_MASK,
				      LIS2DW12_EN_BIT);
#endif /* CONFIG_GESTURE_SENSOR_DOUBLE_TAP */
	return ret;
}

static void lis2dw12_handle_interrupt_for_fifo(uint32_t ts)
{
#ifdef CONFIG_ACCEL_FIFO
	if (time_after(ts, last_interrupt_timestamp))
		last_interrupt_timestamp = ts;
#endif
	task_set_event(TASK_ID_MOTIONSENSE,
		       CONFIG_ACCEL_LIS2DW12_INT_EVENT, 0);
}

/**
 * lis2dw12_interrupt - interrupt from int pin of sensor
 * Schedule Motion Sense Task to manage Interrupts.
 */
void lis2dw12_interrupt(enum gpio_signal signal)
{
	lis2dw12_handle_interrupt_for_fifo(__hw_clock_source_read());
}

/**
 * lis2dw12_irq_handler - bottom half of the interrupt stack.
 */
static int lis2dw12_irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	int ret = EC_SUCCESS;

	if ((s->type != MOTIONSENSE_TYPE_ACCEL) ||
	    (!(*event & CONFIG_ACCEL_LIS2DW12_INT_EVENT))) {
		return EC_ERROR_NOT_HANDLED;
	}

#ifdef CONFIG_GESTURE_SENSOR_DOUBLE_TAP
	{
		int status = 0;

		/* Read Status register to check TAP events. */
		st_raw_read8(s->port, s->i2c_spi_addr_flags,
			     LIS2DW12_STATUS_TAP, &status);
		if (status & LIS2DW12_DOUBLE_TAP)
			*event |= CONFIG_GESTURE_TAP_EVENT;
	}
#endif /* CONFIG_GESTURE_SENSOR_DOUBLE_TAP */

#ifdef CONFIG_ACCEL_FIFO
	{
		int nsamples;
		uint32_t last_fifo_read_ts;
		uint32_t triggering_interrupt_timestamp =
			last_interrupt_timestamp;

		ret = lis2dw12_get_fifo_samples(s, &nsamples);
		if (ret != EC_SUCCESS)
			return ret;

		last_fifo_read_ts = __hw_clock_source_read();
		if (nsamples == 0)
			return EC_SUCCESS;

		ret = lis2dw12_load_fifo(s, nsamples, &last_fifo_read_ts);

		/*
		 * Check if FIFO isn't empty and we never got an interrupt.
		 * This can happen if new entries were added to the FIFO after
		 * the count was read, but before the FIFO was cleared out.
		 * In the long term it might be better to use the last
		 * spread timestamp instead.
		 */
		if (fifo_data_avail(s) &&
		    triggering_interrupt_timestamp == last_interrupt_timestamp)
			lis2dw12_handle_interrupt_for_fifo(last_fifo_read_ts);
	}
#endif /* CONFIG_ACCEL_FIFO */

	return ret;
}

#endif /* CONFIG_ACCEL_LIS2DW_AS_BASE */

/**
 * set_power_mode - set sensor power mode
 * @s: Motion sensor pointer
 * @mode: LIS2DW12_LOW_POWER, LIS2DW12_HIGH_PERF
 * @lpmode: LIS2DW12_LOW_POWER_MODE_2,
 *          LIS2DW12_LOW_POWER_MODE_3,
 *          LIS2DW12_LOW_POWER_MODE_4
 *
 * TODO: LIS2DW12_LOW_POWER_MODE_1 not implemented because output differ
 *       in resolution
 */
static int set_power_mode(const struct motion_sensor_t *s,
			  enum lis2sw12_mode mode,
			  enum lis2sw12_lpmode lpmode)
{
	int ret = EC_SUCCESS;

	if (mode == LIS2DW12_LOW_POWER &&
	    lpmode == LIS2DW12_LOW_POWER_MODE_1)
		return EC_ERROR_UNIMPLEMENTED;

	/* Set Mode and Low Power Mode. */
	ret = st_write_data_with_mask(s, LIS2DW12_ACC_MODE_ADDR,
				      LIS2DW12_ACC_MODE_MASK, mode);
	if (ret != EC_SUCCESS)
		return ret;

	ret = st_write_data_with_mask(s, LIS2DW12_ACC_LPMODE_ADDR,
				      LIS2DW12_ACC_LPMODE_MASK, lpmode);
	return ret;
}

/**
 * set_range - set full scale range
 * @s: Motion sensor pointer
 * @range: Range
 * @rnd: Round up/down flag
 */
static int set_range(const struct motion_sensor_t *s, int range, int rnd)
{
	int err = EC_SUCCESS;
	uint8_t reg_val;
	struct stprivate_data *data = s->drv_data;
	int newrange = range;

	/* Adjust and check rounded value. */
	if (rnd && (newrange < LIS2DW12_NORMALIZE_FS(newrange)))
		newrange <<= 1;

	if (newrange > LIS2DW12_ACCEL_FS_MAX_VAL)
		newrange = LIS2DW12_ACCEL_FS_MAX_VAL;

	reg_val = LIS2DW12_FS_REG(newrange);

	mutex_lock(s->mutex);
#if defined(CONFIG_ACCEL_FIFO) && defined(CONFIG_ACCEL_LIS2DW_AS_BASE)
	/*
	 * FIFO stop collecting events. Restart FIFO in Bypass mode.
	 * If Range is changed all samples in FIFO must be discharged because
	 * with a different sensitivity.
	 */
	err = lis2dw12_enable_fifo(s, LIS2DW12_FIFO_BYPASS_MODE);
	if (err != EC_SUCCESS)
		goto unlock_rate;
#endif /* CONFIG_ACCEL_FIFO && CONFIG_ACCEL_LIS2DW_AS_BASE */

	err = st_write_data_with_mask(s, LIS2DW12_FS_ADDR, LIS2DW12_FS_MASK,
				      reg_val);
	if (err == EC_SUCCESS)
		data->base.range = newrange;
#if defined(CONFIG_ACCEL_FIFO) && defined(CONFIG_ACCEL_LIS2DW_AS_BASE)
	/* FIFO restart collecting events in Cont. mode. */
	err = lis2dw12_enable_fifo(s, LIS2DW12_FIFO_CONT_MODE);

unlock_rate:
#endif /* CONFIG_ACCEL_FIFO && CONFIG_ACCEL_LIS2DW_AS_BASE */
	mutex_unlock(s->mutex);

	return err;
}

static int get_range(const struct motion_sensor_t *s)
{
	struct stprivate_data *data = s->drv_data;

	return data->base.range;
}

/**
 * ODR reg value from selected data rate in mHz.
 */
static uint8_t odr_to_reg(int odr)
{
	if (odr <= LIS2DW12_ODR_MIN_VAL)
		return LIS2DW12_ODR_12HZ_VAL;

	return (__fls(odr / LIS2DW12_ODR_MIN_VAL) + LIS2DW12_ODR_12HZ_VAL);
}

/**
 * Normalized ODR value from selected data rate in mHz.
 */
static int odr_to_normalize(int odr)
{
	if (odr <= LIS2DW12_ODR_MIN_VAL)
		return LIS2DW12_ODR_MIN_VAL;

	return (LIS2DW12_ODR_MIN_VAL << (__fls(odr / LIS2DW12_ODR_MIN_VAL)));
}

static int set_data_rate(const struct motion_sensor_t *s, int rate, int rnd)
{
	int ret, normalized_rate;
	struct stprivate_data *data = s->drv_data;
	uint8_t reg_val;

	mutex_lock(s->mutex);

#if defined(CONFIG_ACCEL_FIFO) && defined(CONFIG_ACCEL_LIS2DW_AS_BASE)
	/* FIFO stop collecting events. Restart FIFO in Bypass mode. */
	ret = lis2dw12_enable_fifo(s, LIS2DW12_FIFO_BYPASS_MODE);
	if (ret != EC_SUCCESS)
		goto unlock_rate;
#endif /* CONFIG_ACCEL_FIFO && CONFIG_ACCEL_LIS2DW_AS_BASE */

	if (rate == 0) {
		ret = st_write_data_with_mask(s, LIS2DW12_ACC_ODR_ADDR,
					      LIS2DW12_ACC_ODR_MASK,
					      LIS2DW12_ODR_POWER_OFF_VAL);
		if (ret == EC_SUCCESS)
			data->base.odr = LIS2DW12_ODR_POWER_OFF_VAL;

		goto unlock_rate;
	}

	reg_val = odr_to_reg(rate);
	normalized_rate = odr_to_normalize(rate);

	if (rnd && (normalized_rate < rate)) {
		reg_val++;
		normalized_rate <<= 1;
	}

	if (reg_val > LIS2DW12_ODR_1_6kHZ_VAL) {
		reg_val = LIS2DW12_ODR_1_6kHZ_VAL;
		normalized_rate = LIS2DW12_ODR_MAX_VAL;
	} else if (reg_val < LIS2DW12_ODR_12HZ_VAL) {
		reg_val = LIS2DW12_ODR_12HZ_VAL;
		normalized_rate = LIS2DW12_ODR_MIN_VAL;
	}

	/* lis2dwl supports 14 bit resolution only at high performance mode,
	 * and it will always stay at high performance mode from initialization.
	 * But lis2dw12 needs switch low power mode according to odr value.
	 */
#ifndef CONFIG_ACCEL_LIS2DWL
	if (reg_val > LIS2DW12_ODR_200HZ_VAL)
		ret = set_power_mode(s, LIS2DW12_HIGH_PERF, 0);
	else
		ret = set_power_mode(s, LIS2DW12_LOW_POWER,
				     LIS2DW12_LOW_POWER_MODE_2);
#endif

	ret = st_write_data_with_mask(s, LIS2DW12_ACC_ODR_ADDR,
				      LIS2DW12_ACC_ODR_MASK, reg_val);
	if (ret == EC_SUCCESS)
		data->base.odr = normalized_rate;

#if defined(CONFIG_ACCEL_FIFO) && defined(CONFIG_ACCEL_LIS2DW_AS_BASE)
	/* FIFO restart collecting events in continuous mode. */
	ret = lis2dw12_enable_fifo(s, LIS2DW12_FIFO_CONT_MODE);
#endif /* CONFIG_ACCEL_FIFO && CONFIG_ACCEL_LIS2DW_AS_BASE */

unlock_rate:
	mutex_unlock(s->mutex);

	return ret;
}

static int is_data_ready(const struct motion_sensor_t *s, int *ready)
{
	int ret, tmp;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags,
			   LIS2DW12_STATUS_REG, &tmp);
	if (ret != EC_SUCCESS)
		return ret;

	*ready = (LIS2DW12_STS_DRDY_UP == (tmp & LIS2DW12_STS_DRDY_UP));

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

	/* Read 6 bytes starting at xyz_reg. */
	ret = st_raw_read_n_noinc(s->port, s->i2c_spi_addr_flags,
				  LIS2DW12_OUT_X_L_ADDR, raw,
				  OUT_XYZ_SIZE);
	if (ret != EC_SUCCESS) {
		CPRINTS("%s type:0x%X RD XYZ Error", s->name, s->type);
		return ret;
	}

	/* Transform from LSB to real data with rotation and gain. */
	st_normalize(s, v, raw);

	return EC_SUCCESS;
}

static int init(const struct motion_sensor_t *s)
{
	int ret = 0, tmp, timeout = 0, status;
	struct stprivate_data *data = s->drv_data;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags,
			   LIS2DW12_WHO_AM_I_REG, &tmp);
	if (ret != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	if (tmp != LIS2DW12_WHO_AM_I)
		return EC_ERROR_ACCESS_DENIED;

	/*
	 * This sensor can be powered through an EC reboot, so the state of
	 * the sensor is unknown here. Initiate software reset to restore
	 * sensor to default.
	 */
	mutex_lock(s->mutex);
	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
			    LIS2DW12_SOFT_RESET_ADDR, LIS2DW12_SOFT_RESET_MASK);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	/* Wait End of Reset. */
	do {
		if (timeout > 10) {
			ret = EC_RES_TIMEOUT;
			goto err_unlock;
		}

		msleep(1);
		timeout += 1;
		ret = st_raw_read8(s->port, s->i2c_spi_addr_flags,
				  LIS2DW12_SOFT_RESET_ADDR, &status);
		if (ret != EC_SUCCESS)
			continue;
	} while ((status & LIS2DW12_SOFT_RESET_MASK) != 0);

	/* Enable BDU. */
	ret = st_write_data_with_mask(s, LIS2DW12_BDU_ADDR, LIS2DW12_BDU_MASK,
				      LIS2DW12_EN_BIT);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	ret = st_write_data_with_mask(s, LIS2DW12_LIR_ADDR, LIS2DW12_LIR_MASK,
				      LIS2DW12_EN_BIT);
	if (ret != EC_SUCCESS)
		goto err_unlock;

#if defined(CONFIG_ACCEL_INTERRUPTS) && defined(CONFIG_ACCEL_LIS2DW_AS_BASE)
	/* Interrupt trigger level of power-on-reset is HIGH */
	if (!(MOTIONSENSE_FLAG_INT_ACTIVE_HIGH & s->flags)) {
		ret = st_write_data_with_mask(s, LIS2DW12_H_ACTIVE_ADDR,
						LIS2DW12_H_ACTIVE_MASK,
						LIS2DW12_EN_BIT);
		if (ret != EC_SUCCESS)
			goto err_unlock;
	}
#endif

#ifdef CONFIG_ACCEL_LIS2DWL
	/* lis2dwl supports 14 bit resolution only at high perfomance mode */
	ret = set_power_mode(s, LIS2DW12_HIGH_PERF, 0);
#else
	/* Set default Mode and Low Power Mode. */
	ret = set_power_mode(s, LIS2DW12_LOW_POWER, LIS2DW12_LOW_POWER_MODE_2);
#endif
	if (ret != EC_SUCCESS)
		goto err_unlock;

#ifdef CONFIG_ACCEL_LIS2DW_AS_BASE
	if (IS_ENABLED(CONFIG_ACCEL_INTERRUPTS)) {
		ret = lis2dw12_config_interrupt(s);
		if (ret != EC_SUCCESS)
			goto err_unlock;
	}
#endif
	mutex_unlock(s->mutex);

	/* Set default resolution. */
	data->resol = LIS2DW12_RESOLUTION;
	return sensor_init_done(s);

err_unlock:
	mutex_unlock(s->mutex);
	CPRINTS("%s: MS Init type:0x%X Error", s->name, s->type);
	return EC_ERROR_UNKNOWN;
}

const struct accelgyro_drv lis2dw12_drv = {
	.init = init,
	.read = read,
	.set_range = set_range,
	.get_range = get_range,
	.get_resolution = st_get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = st_get_data_rate,
	.set_offset = st_set_offset,
	.get_offset = st_get_offset,
#if defined(CONFIG_ACCEL_INTERRUPTS) && defined(CONFIG_ACCEL_LIS2DW_AS_BASE)
	.irq_handler = lis2dw12_irq_handler,
#endif /* CONFIG_ACCEL_INTERRUPTS && CONFIG_ACCEL_LIS2DW_AS_BASE */
};
