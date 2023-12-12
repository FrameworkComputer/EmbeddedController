/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * LIS2DW12 accelerometer module for Chrome EC 3D digital accelerometer.
 * For more details on LIS2DW12 device please refers to www.st.com.
 */
#include "accel_lis2dw12.h"
#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "hwtimer.h"
#include "math_util.h"
#include "motion_sense_fifo.h"
#include "task.h"
#include "util.h"

#ifdef CONFIG_ACCEL_LIS2DW12_INT_EVENT
#define ACCEL_LIS2DW12_INT_ENABLE
#endif

#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ##args)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ##args)

STATIC_IF(ACCEL_LIS2DW12_INT_ENABLE)
volatile uint32_t last_interrupt_timestamp;

/**
 * lis2dw12_enable_fifo - Enable/Disable FIFO in LIS2DW12
 * @s: Motion sensor pointer
 * @mode: fifo_modes
 */
static __maybe_unused int lis2dw12_enable_fifo(const struct motion_sensor_t *s,
					       enum lis2dw12_fmode mode)
{
	return st_write_data_with_mask(s, LIS2DW12_FIFO_CTRL_ADDR,
				       LIS2DW12_FIFO_MODE_MASK, mode);
}

/**
 * lis2dw12_config_interrupt- Configure interrupt for supported features.
 * @s: Motion sensor pointer
 *
 * Must works with interface mutex locked
 */
static __maybe_unused int
lis2dw12_config_interrupt(const struct motion_sensor_t *s)
{
	/* Configure FIFO watermark level. */
	RETURN_ERROR(st_write_data_with_mask(s, LIS2DW12_FIFO_CTRL_ADDR,
					     LIS2DW12_FIFO_THRESHOLD_MASK, 1));

	/* Enable interrupt on FIFO watermark and route to int1. */
	RETURN_ERROR(st_write_data_with_mask(s, LIS2DW12_INT1_FTH_ADDR,
					     LIS2DW12_INT1_FTH_MASK,
					     LIS2DW12_EN_BIT));

	if (IS_ENABLED(CONFIG_GESTURE_SENSOR_DOUBLE_TAP)) {
		/*
		 * Configure D-TAP event detection on 3 axis.
		 * For more details please refer to AN5038.
		 */
		RETURN_ERROR(st_raw_write8(s->port, s->i2c_spi_addr_flags,
					   LIS2DW12_TAP_THS_X_ADDR, 0x09));
		RETURN_ERROR(st_raw_write8(s->port, s->i2c_spi_addr_flags,
					   LIS2DW12_TAP_THS_Y_ADDR, 0x09));
		RETURN_ERROR(st_raw_write8(s->port, s->i2c_spi_addr_flags,
					   LIS2DW12_TAP_THS_Z_ADDR, 0xE9));
		RETURN_ERROR(st_raw_write8(s->port, s->i2c_spi_addr_flags,
					   LIS2DW12_INT_DUR_ADDR, 0x7F));

		/* Enable D-TAP event detection. */
		RETURN_ERROR(st_write_data_with_mask(
			s, LIS2DW12_WAKE_UP_THS_ADDR,
			LIS2DW12_SINGLE_DOUBLE_TAP, LIS2DW12_EN_BIT));

		/*
		 * Enable D-TAP detection on int_1 pad. In any case D-TAP event
		 * can be detected only if ODR is over 200 Hz.
		 */
		RETURN_ERROR(st_write_data_with_mask(s, LIS2DW12_INT1_TAP_ADDR,
						     LIS2DW12_INT1_DTAP_MASK,
						     LIS2DW12_EN_BIT));
	}
	return EC_SUCCESS;
}

#ifdef ACCEL_LIS2DW12_INT_ENABLE
/**
 * Load data from internal sensor FIFO.
 * @s: Motion sensor pointer
 */
static int lis2dw12_load_fifo(struct motion_sensor_t *s, int nsamples)
{
	int ret, left, length, i;
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
		if (ret != EC_SUCCESS)
			return ret;

		for (i = 0; i < length; i += OUT_XYZ_SIZE) {
			/* Apply precision, sensitivity and rotation vector. */
			st_normalize(s, axis, &fifo[i]);

			if (IS_ENABLED(CONFIG_ACCEL_SPOOF_MODE) &&
			    s->flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE)
				axis = s->spoof_xyz;
			if (IS_ENABLED(CONFIG_ACCEL_FIFO)) {
				struct ec_response_motion_sensor_data vect;
				/* Fill vector array. */
				vect.data[X] = axis[X];
				vect.data[Y] = axis[Y];
				vect.data[Z] = axis[Z];
				vect.flags = 0;
				vect.sensor_num = s - motion_sensors;
				motion_sense_fifo_stage_data(
					&vect, s, 3, interrupt_timestamp);
			} else {
				motion_sense_push_raw_xyz(s);
			}
			left -= length;
		}
	} while (left > 0);

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

/**
 * lis2dw12_interrupt - interrupt from int pin of sensor
 * Schedule Motion Sense Task to manage Interrupts.
 */
test_mockable void lis2dw12_interrupt(enum gpio_signal signal)
{
	last_interrupt_timestamp = __hw_clock_source_read();

	task_set_event(TASK_ID_MOTIONSENSE, CONFIG_ACCEL_LIS2DW12_INT_EVENT);
}

/**
 * lis2dw12_irq_handler - bottom half of the interrupt stack.
 */
static int lis2dw12_irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	bool commit_needed = false;
	int nsamples;

	if ((s->type != MOTIONSENSE_TYPE_ACCEL) ||
	    (!(*event & CONFIG_ACCEL_LIS2DW12_INT_EVENT))) {
		return EC_ERROR_NOT_HANDLED;
	}

	if (IS_ENABLED(CONFIG_GESTURE_SENSOR_DOUBLE_TAP)) {
		int status = 0;

		/* Read Status register to check TAP events. */
		st_raw_read8(s->port, s->i2c_spi_addr_flags,
			     LIS2DW12_STATUS_TAP, &status);
		if (status & LIS2DW12_DOUBLE_TAP)
			*event |= TASK_EVENT_MOTION_ACTIVITY_INTERRUPT(
				MOTIONSENSE_ACTIVITY_DOUBLE_TAP);
	}

	do {
		RETURN_ERROR(lis2dw12_get_fifo_samples(s, &nsamples));

		if (nsamples != 0) {
			commit_needed = true;
			RETURN_ERROR(lis2dw12_load_fifo(s, nsamples));
		}
	} while (nsamples != 0);

	if (IS_ENABLED(CONFIG_ACCEL_FIFO) && commit_needed)
		motion_sense_fifo_commit_data();

	return EC_SUCCESS;
}
#endif

/**
 * lis2dw12_set_power_mode - set sensor power mode
 * @s: Motion sensor pointer
 * @mode: LIS2DW12_LOW_POWER, LIS2DW12_HIGH_PERF
 * @lpmode: LIS2DW12_LOW_POWER_MODE_2,
 *          LIS2DW12_LOW_POWER_MODE_3,
 *          LIS2DW12_LOW_POWER_MODE_4
 *
 * TODO: LIS2DW12_LOW_POWER_MODE_1 not implemented because output differ
 *       in resolution
 */
STATIC_IF_NOT(CONFIG_ZTEST)
int lis2dw12_set_power_mode(const struct motion_sensor_t *s,
			    enum lis2sw12_mode mode,
			    enum lis2sw12_lpmode lpmode)
{
	int ret = EC_SUCCESS;

	if (mode == LIS2DW12_LOW_POWER && lpmode == LIS2DW12_LOW_POWER_MODE_1)
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
static int set_range(struct motion_sensor_t *s, int range, int rnd)
{
	int err = EC_SUCCESS;
	uint8_t reg_val;
	int newrange = range;

	/* Adjust and check rounded value. */
	if (rnd && (newrange < LIS2DW12_NORMALIZE_FS(newrange)))
		newrange <<= 1;

	if (newrange > LIS2DW12_ACCEL_FS_MAX_VAL)
		newrange = LIS2DW12_ACCEL_FS_MAX_VAL;

	reg_val = LIS2DW12_FS_REG(newrange);

	mutex_lock(s->mutex);
	/*
	 * FIFO stop collecting events. Restart FIFO in Bypass mode.
	 * If Range is changed all samples in FIFO must be discharged because
	 * with a different sensitivity.
	 */
	if (IS_ENABLED(ACCEL_LIS2DW12_INT_ENABLE)) {
		err = lis2dw12_enable_fifo(s, LIS2DW12_FIFO_BYPASS_MODE);
		if (err != EC_SUCCESS)
			goto unlock_rate;
	}

	err = st_write_data_with_mask(s, LIS2DW12_FS_ADDR, LIS2DW12_FS_MASK,
				      reg_val);
	if (err == EC_SUCCESS)
		s->current_range = newrange;
	else
		goto unlock_rate;

	/* FIFO restart collecting events in Cont. mode. */
	if (IS_ENABLED(ACCEL_LIS2DW12_INT_ENABLE))
		err = lis2dw12_enable_fifo(s, LIS2DW12_FIFO_CONT_MODE);

unlock_rate:
	mutex_unlock(s->mutex);

	return err;
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

	/* FIFO stop collecting events. Restart FIFO in Bypass mode. */
	if (IS_ENABLED(ACCEL_LIS2DW12_INT_ENABLE)) {
		ret = lis2dw12_enable_fifo(s, LIS2DW12_FIFO_BYPASS_MODE);
		if (ret != EC_SUCCESS)
			goto unlock_rate;
	}

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
	}

	/* lis2dwl supports 14 bit resolution only at high performance mode,
	 * and it will always stay at high performance mode from initialization.
	 * But lis2dw12 needs switch low power mode according to odr value.
	 */
	if (!IS_ENABLED(CONFIG_ACCEL_LIS2DWL)) {
		if (reg_val >= LIS2DW12_ODR_50HZ_VAL)
			ret = lis2dw12_set_power_mode(s, LIS2DW12_HIGH_PERF, 0);
		else
			ret = lis2dw12_set_power_mode(
				s, LIS2DW12_LOW_POWER,
				LIS2DW12_LOW_POWER_MODE_2);
	}

	ret = st_write_data_with_mask(s, LIS2DW12_ACC_ODR_ADDR,
				      LIS2DW12_ACC_ODR_MASK, reg_val);
	if (ret == EC_SUCCESS)
		data->base.odr = normalized_rate;

	/* FIFO restart collecting events in continuous mode. */
	if (IS_ENABLED(ACCEL_LIS2DW12_INT_ENABLE))
		ret = lis2dw12_enable_fifo(s, LIS2DW12_FIFO_CONT_MODE);

unlock_rate:
	mutex_unlock(s->mutex);

	return ret;
}

#ifdef CONFIG_BODY_DETECTION
static int get_rms_noise(const struct motion_sensor_t *s)
{
	fp_t rate, noise_density_ug;

	/* change unit of ODR to Hz to prevent INT_TO_FP() overflow */
	rate = INT_TO_FP(st_get_data_rate(s) / 1000);

	/*
	 * LIS2DW12: 90ug/sqrt(Hz) when ODR is over 50Hz
	 * When lower, we are in power mode 2, so the noise density does not
	 * depend on frequency and the RMS at +/-2g is 2.4mg.
	 *
	 * LIS12DWL: 110uq/sqr(Hz) for all frequencies, since low power mode
	 * is not used.
	 */

	if (!IS_ENABLED(CONFIG_ACCEL_LIS2DWL)) {
		if (rate < INT_TO_FP(50))
			return 2400;
		noise_density_ug = INT_TO_FP(90);
	} else {
		noise_density_ug = INT_TO_FP(110);
	}

	return FP_TO_INT(fp_mul(fp_sqrtf(rate), noise_density_ug));
}
#endif

static int is_data_ready(const struct motion_sensor_t *s, int *ready)
{
	int ret, tmp;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags, LIS2DW12_STATUS_REG,
			   &tmp);
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
				  LIS2DW12_OUT_X_L_ADDR, raw, OUT_XYZ_SIZE);
	if (ret != EC_SUCCESS) {
		CPRINTS("%s type:0x%X RD XYZ Error", s->name, s->type);
		return ret;
	}

	/* Transform from LSB to real data with rotation and gain. */
	st_normalize(s, v, raw);

	return EC_SUCCESS;
}

static int init(struct motion_sensor_t *s)
{
	int ret = 0, tmp, timeout = 0, status;
	struct stprivate_data *data = s->drv_data;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags,
			   LIS2DW12_WHO_AM_I_REG, &tmp);
	if (ret != EC_SUCCESS)
		return ret;

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
			ret = EC_ERROR_TIMEOUT;
			goto err_unlock;
		}

		msleep(1);
		timeout += 1;
		ret = st_raw_read8(s->port, s->i2c_spi_addr_flags,
				   LIS2DW12_SOFT_RESET_ADDR, &status);
	} while (ret != EC_SUCCESS || (status & LIS2DW12_SOFT_RESET_MASK) != 0);

	/* Enable BDU. */
	ret = st_write_data_with_mask(s, LIS2DW12_BDU_ADDR, LIS2DW12_BDU_MASK,
				      LIS2DW12_EN_BIT);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	ret = st_write_data_with_mask(s, LIS2DW12_LIR_ADDR, LIS2DW12_LIR_MASK,
				      LIS2DW12_EN_BIT);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	ret = st_write_data_with_mask(s, LIS2DW12_LOW_NOISE_ADDR,
				      LIS2DW12_LOW_NOISE_MASK, LIS2DW12_EN_BIT);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	/* Interrupt trigger level of power-on-reset is HIGH */
	if (IS_ENABLED(ACCEL_LIS2DW12_INT_ENABLE)) {
		ret = st_write_data_with_mask(s, LIS2DW12_H_ACTIVE_ADDR,
					      LIS2DW12_H_ACTIVE_MASK,
					      LIS2DW12_EN_BIT);
		if (ret != EC_SUCCESS)
			goto err_unlock;
	}

	if (IS_ENABLED(CONFIG_ACCEL_LIS2DWL))
		/*
		 * lis2dwl supports 14 bit resolution only
		 * at high performance mode
		 */
		ret = lis2dw12_set_power_mode(s, LIS2DW12_HIGH_PERF, 0);
	else
		/* Set default Mode and Low Power Mode. */
		ret = lis2dw12_set_power_mode(s, LIS2DW12_LOW_POWER,
					      LIS2DW12_LOW_POWER_MODE_2);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	if (IS_ENABLED(ACCEL_LIS2DW12_INT_ENABLE)) {
		ret = lis2dw12_config_interrupt(s);
		if (ret != EC_SUCCESS)
			goto err_unlock;
	}
	mutex_unlock(s->mutex);

	/* Set default resolution. */
	data->resol = LIS2DW12_RESOLUTION;
	return sensor_init_done(s);

err_unlock:
	mutex_unlock(s->mutex);
	CPRINTS("%s: MS Init type:0x%X Error(%d)", s->name, s->type, ret);
	return ret;
}

const struct accelgyro_drv lis2dw12_drv = {
	.init = init,
	.read = read,
	.set_range = set_range,
	.get_resolution = st_get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = st_get_data_rate,
	.set_offset = st_set_offset,
	.get_offset = st_get_offset,
#ifdef ACCEL_LIS2DW12_INT_ENABLE
	.irq_handler = lis2dw12_irq_handler,
#endif /* ACCEL_LIS2DW12_INT_ENABLE */
#ifdef CONFIG_BODY_DETECTION
	.get_rms_noise = get_rms_noise,
#endif
};
