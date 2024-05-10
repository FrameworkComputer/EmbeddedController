/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * LIS2DS Accel module for Chrome EC
 * MEMS digital output motion sensor:
 * ultra-low-power high-performance 3-axis "pico" accelerometer
 *
 * For any details on driver implementation please
 * Refer to AN4748 Application Note on www.st.com
 */
#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "driver/accel_lis2ds.h"
#include "hooks.h"
#include "hwtimer.h"
#include "i2c.h"
#include "math_util.h"
#include "motion_sense_fifo.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#ifdef CONFIG_ACCEL_LIS2DS_INT_EVENT
#define ACCEL_LIS2DS_INT_ENABLE
#endif

#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ##args)

STATIC_IF(ACCEL_LIS2DS_INT_ENABLE)
volatile uint32_t last_interrupt_timestamp;

/**
 * lis2ds_enable_fifo - Enable/Disable FIFO in LIS2DS12
 * @s: Motion sensor pointer
 * @mode: fifo_modes
 */
static int lis2ds_enable_fifo(const struct motion_sensor_t *s, int mode)
{
	return st_write_data_with_mask(s, LIS2DS_FIFO_CTRL_ADDR,
				       LIS2DS_FIFO_MODE_MASK, mode);
}

static int lis2ds_config_interrupt(const struct motion_sensor_t *s)
{
	int ret = EC_SUCCESS;

	/* Interrupt trigger level of power-on-reset is HIGH */
	RETURN_ERROR(st_write_data_with_mask(
		s, LIS2DS_H_ACTIVE_ADDR, LIS2DS_H_ACTIVE_MASK, LIS2DS_EN_BIT));

	/*
	 * Configure FIFO threshold to 1 sample: interrupt on watermark
	 * will be generated every time a new data sample will be stored
	 * in FIFO. The interrupr on watermark is cleared only when the
	 * number or samples still present in FIFO exceeds the
	 * configured threshold.
	 */
	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
			    LIS2DS_FIFO_THS_ADDR, 1);
	if (ret != EC_SUCCESS)
		return ret;

	/* Enable interrupt on FIFO watermark and route to int1. */
	ret = st_write_data_with_mask(s, LIS2DS_CTRL4_ADDR, LIS2DS_INT1_FTH,
				      LIS2DS_EN_BIT);

	return ret;
}

#ifdef ACCEL_LIS2DS_INT_ENABLE
/**
 * Load data from internal sensor FIFO
 * DIFF8 bits set means FIFO Full because 256 samples -> 0x100
 */
static int lis2ds_load_fifo(struct motion_sensor_t *s, uint16_t nsamples,
			    uint32_t saved_ts)
{
	int ret, read_len, fifo_len, chunk_len, i;
	int *axis = s->raw_xyz;
	uint8_t fifo[FIFO_READ_LEN];

	fifo_len = nsamples * OUT_XYZ_SIZE;
	read_len = 0;

	while (read_len < fifo_len) {
		chunk_len = GENERIC_MIN(fifo_len - read_len, sizeof(fifo));

		ret = st_raw_read_n_noinc(s->port, s->i2c_spi_addr_flags,
					  LIS2DS_OUT_X_L_ADDR, fifo, chunk_len);
		if (ret != EC_SUCCESS)
			return ret;

		for (i = 0; i < chunk_len; i += OUT_XYZ_SIZE) {
			/* Apply precision, sensitivity and rotation vector */
			st_normalize(s, axis, &fifo[i]);

			if (IS_ENABLED(CONFIG_ACCEL_SPOOF_MODE) &&
			    s->flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE)
				axis = s->spoof_xyz;
			if (IS_ENABLED(CONFIG_ACCEL_FIFO)) {
				struct ec_response_motion_sensor_data vect;

				/* Fill vector array */
				vect.data[0] = axis[0];
				vect.data[1] = axis[1];
				vect.data[2] = axis[2];
				vect.flags = 0;
				vect.sensor_num = s - motion_sensors;
				motion_sense_fifo_stage_data(&vect, s, 3,
							     saved_ts);
			} else {
				motion_sense_push_raw_xyz(s);
			}
		}

		read_len += chunk_len;
	};

	if (read_len > 0)
		motion_sense_fifo_commit_data();

	return EC_SUCCESS;
}

/**
 * lis2ds_interrupt - interrupt from int1 pin of sensor
 * Schedule Motion Sense Task to manage Interrupts
 */
void lis2ds_interrupt(enum gpio_signal signal)
{
	last_interrupt_timestamp = __hw_clock_source_read();

	task_set_event(TASK_ID_MOTIONSENSE, CONFIG_ACCEL_LIS2DS_INT_EVENT);
}

/**
 * lis2ds_irq_handler - bottom half of the interrupt stack.
 */
static int lis2ds_irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	int ret = EC_SUCCESS;
	uint16_t nsamples = 0;
	uint8_t fifo_src_samples[2];

	if ((s->type != MOTIONSENSE_TYPE_ACCEL) ||
	    (!(*event & CONFIG_ACCEL_LIS2DS_INT_EVENT)))
		return EC_ERROR_NOT_HANDLED;

	ret = st_raw_read_n_noinc(s->port, s->i2c_spi_addr_flags,
				  LIS2DS_FIFO_SRC_ADDR,
				  (uint8_t *)fifo_src_samples,
				  sizeof(fifo_src_samples));
	if (ret != EC_SUCCESS)
		return ret;

	/* Check if FIFO is full. */
	if (fifo_src_samples[0] & LIS2DS_FIFO_OVR_MASK)
		CPRINTS("%s FIFO Overrun", s->name);

	/* DIFF8 = 1 FIFO FULL, 256 unread samples. */
	nsamples = fifo_src_samples[1] & LIS2DS_FIFO_DIFF_MASK;
	if (fifo_src_samples[0] & LIS2DS_FIFO_DIFF8_MASK)
		nsamples = 256;

	return lis2ds_load_fifo(s, nsamples, last_interrupt_timestamp);
}

#endif /* ACCEL_LIS2DS_INT_ENABLE */

/**
 * set_range - set full scale range
 * @s: Motion sensor pointer
 * @range: Range
 * @rnd: Round up/down flag
 */
static int set_range(struct motion_sensor_t *s, int range, int rnd)
{
	int err;
	uint8_t reg_val;
	int newrange = ST_NORMALIZE_RATE(range);

	/* Adjust and check rounded value */
	if (rnd && (newrange < range))
		newrange <<= 1;

	if (newrange > LIS2DS_ACCEL_FS_MAX_VAL)
		newrange = LIS2DS_ACCEL_FS_MAX_VAL;
	else if (newrange < LIS2DS_ACCEL_FS_MIN_VAL)
		newrange = LIS2DS_ACCEL_FS_MIN_VAL;

	reg_val = LIS2DS_FS_REG(newrange);

	mutex_lock(s->mutex);
	err = st_write_data_with_mask(s, LIS2DS_FS_ADDR, LIS2DS_FS_MASK,
				      reg_val);
	if (err == EC_SUCCESS)
		/* Save internally gain for speed optimization. */
		s->current_range = newrange;
	mutex_unlock(s->mutex);

	return EC_SUCCESS;
}

static int set_data_rate(const struct motion_sensor_t *s, int rate, int rnd)
{
	int ret, normalized_rate = 0;
	struct stprivate_data *data = s->drv_data;
	uint8_t reg_val = 0;

	mutex_lock(s->mutex);
	if (IS_ENABLED(ACCEL_LIS2DS_INT_ENABLE)) {
		/* FIFO stop collecting events. Restart FIFO in Bypass mode */
		ret = lis2ds_enable_fifo(s, LIS2DS_FIFO_BYPASS_MODE);
		if (ret != EC_SUCCESS)
			goto unlock_rate;
	}

	/* Avoid LIS2DS_ODR_TO_REG to manage 0 mHz rate */
	if (rate > 0) {
		reg_val = LIS2DS_ODR_TO_REG(rate);
		normalized_rate = LIS2DS_REG_TO_ODR(reg_val);

		if (rnd && (normalized_rate < rate)) {
			reg_val++;
			normalized_rate = LIS2DS_REG_TO_ODR(rate);
		}

		if (normalized_rate < LIS2DS_ODR_MIN_VAL ||
		    normalized_rate > LIS2DS_ODR_MAX_VAL) {
			ret = EC_RES_INVALID_PARAM;
			goto unlock_rate;
		}
	}

	ret = st_write_data_with_mask(s, LIS2DS_ACC_ODR_ADDR,
				      LIS2DS_ACC_ODR_MASK, reg_val);
	if (ret == EC_SUCCESS) {
		data->base.odr = normalized_rate;

		if (IS_ENABLED(ACCEL_LIS2DS_INT_ENABLE)) {
			/* FIFO restart collecting events in Cont. mode. */
			ret = lis2ds_enable_fifo(s, LIS2DS_FIFO_CONT_MODE);
		}
	}

unlock_rate:
	mutex_unlock(s->mutex);

	return ret;
}

static int is_data_ready(const struct motion_sensor_t *s, int *ready)
{
	int ret, tmp;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags, LIS2DS_STATUS_REG,
			   &tmp);
	if (ret != EC_SUCCESS) {
		CPRINTS("%s: type:0x%X RD XYZ Error %d", s->name, s->type, ret);
		return ret;
	}

	*ready = (LIS2DS_STS_XLDA_UP == (tmp & LIS2DS_STS_XLDA_UP));

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

	/* Read 6 bytes starting at xyz_reg */
	ret = st_raw_read_n_noinc(s->port, s->i2c_spi_addr_flags,
				  LIS2DS_OUT_X_L_ADDR, raw,
				  LIS2DS_OUT_XYZ_SIZE);
	if (ret != EC_SUCCESS) {
		CPRINTS("%s: type:0x%X RD XYZ Error %d", s->name, s->type, ret);
		return ret;
	}

	/* Transform from LSB to real data with rotation and gain */
	st_normalize(s, v, raw);

	return EC_SUCCESS;
}

static int init(struct motion_sensor_t *s)
{
	int ret = 0, tmp;
	struct stprivate_data *data = s->drv_data;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags, LIS2DS_WHO_AM_I_REG,
			   &tmp);
	if (ret != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	if (tmp != LIS2DS_WHO_AM_I)
		return EC_ERROR_ACCESS_DENIED;

	/*
	 * This sensor can be powered through an EC reboot, so the state of
	 * the sensor is unknown here. Initiate software reset to restore
	 * sensor to default.
	 */
	mutex_lock(s->mutex);

	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
			    LIS2DS_SOFT_RESET_ADDR, LIS2DS_SOFT_RESET_MASK);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	crec_msleep(20);

	/* Enable BDU */
	ret = st_write_data_with_mask(s, LIS2DS_BDU_ADDR, LIS2DS_BDU_MASK,
				      LIS2DS_EN_BIT);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	ret = st_write_data_with_mask(s, LIS2DS_LIR_ADDR, LIS2DS_LIR_MASK,
				      LIS2DS_EN_BIT);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	ret = st_write_data_with_mask(s, LIS2DS_INT2_ON_INT1_ADDR,
				      LIS2DS_INT2_ON_INT1_MASK, LIS2DS_EN_BIT);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	if (IS_ENABLED(ACCEL_LIS2DS_INT_ENABLE))
		ret = lis2ds_config_interrupt(s);
	if (ret != EC_SUCCESS)
		goto err_unlock;

	mutex_unlock(s->mutex);

	/* Set default resolution */
	data->resol = LIS2DS_RESOLUTION;

	return sensor_init_done(s);

err_unlock:
	mutex_unlock(s->mutex);
	CPRINTS("%s: MS Init type:0x%X Error", s->name, s->type);

	return ret;
}

const struct accelgyro_drv lis2ds_drv = {
	.init = init,
	.read = read,
	.set_range = set_range,
	.get_resolution = st_get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = st_get_data_rate,
	.set_offset = st_set_offset,
	.get_offset = st_get_offset,
#ifdef ACCEL_LIS2DS_INT_ENABLE
	.irq_handler = lis2ds_irq_handler,
#endif /* ACCEL_LIS2DS_INT_ENABLE */
};
