/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * LSM6DSO Accel and Gyro module for Chrome EC
 * 3D digital accelerometer & 3D digital gyroscope
 *
 * For any details on driver implementation please
 * Refer to AN5192 Application Note on www.st.com
 */

#include "driver/accelgyro_lsm6dso.h"
#include "hooks.h"
#include "hwtimer.h"
#include "math_util.h"
#include "motion_sense_fifo.h"
#include "task.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ## args)

STATIC_IF(CONFIG_ACCEL_FIFO) volatile uint32_t last_interrupt_timestamp;

/*
 * When ODR change, the sensor filters need settling time;
 * Add a counter to discard a well known number of data with
 * incorrect values.
 */
static uint32_t samples_to_discard[LSM6DSO_FIFO_DEV_NUM];

/**
 * @return output data base register for sensor
 */
static inline int get_xyz_reg(enum motionsensor_type type)
{
	return LSM6DSO_ACCEL_OUT_X_L_ADDR -
	       (LSM6DSO_ACCEL_OUT_X_L_ADDR - LSM6DSO_GYRO_OUT_X_L_ADDR) * type;
}

#ifdef CONFIG_ACCEL_INTERRUPTS
/**
 * Configure interrupt int 1 to fire handler for:
 *
 * FIFO threshold on watermark (1 sample)
 *
 * @s: Motion sensor pointer
 */
static int config_interrupt(const struct motion_sensor_t *s)
{
	int ret = EC_SUCCESS;
	int int1_ctrl_val;

	if (!IS_ENABLED(CONFIG_ACCEL_FIFO))
		return ret;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags, LSM6DSO_INT1_CTRL,
			   &int1_ctrl_val);
	if (ret != EC_SUCCESS)
		return ret;

	/*
	 * Configure FIFO threshold to 1 sample: interrupt on watermark
	 * will be generated every time a new data sample will be stored
	 * in FIFO. The interrupr on watermark is cleared only when the
	 * number or samples still present in FIFO exceeds the
	 * configured threshold.
	 */
	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
			    LSM6DSO_FIFO_CTRL1_ADDR, 1);
	if (ret != EC_SUCCESS)
		return ret;

	int1_ctrl_val |= LSM6DSO_INT_FIFO_TH | LSM6DSO_INT_FIFO_OVR |
		LSM6DSO_INT_FIFO_FULL;

	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags, LSM6DSO_INT1_CTRL,
			    int1_ctrl_val);

	return ret;
}

/**
 * fifo_disable - set fifo mode to LSM6DSO_FIFO_MODE_BYPASS_VAL
 * @s: Motion sensor pointer: must be MOTIONSENSE_TYPE_ACCEL.
 */
static int fifo_disable(const struct motion_sensor_t *s)
{
	return st_raw_write8(s->port, s->i2c_spi_addr_flags,
			     LSM6DSO_FIFO_CTRL4_ADDR,
			     LSM6DSO_FIFO_MODE_BYPASS_VAL);
}

/**
 * set_fifo_params - Configure internal FIFO parameters
 *
 * Configure FIFO decimator to have every time the right pattern
 * with acc/gyro
 */
static int fifo_enable(const struct motion_sensor_t *s)
{
	return st_raw_write8(s->port, s->i2c_spi_addr_flags,
			     LSM6DSO_FIFO_CTRL4_ADDR,
			     LSM6DSO_FIFO_MODE_CONTINUOUS_VAL);
}

/**
 * push_fifo_data - Scan data pattern and push upside
 */
static void push_fifo_data(struct motion_sensor_t *main_s, uint8_t *fifo,
			   uint32_t saved_ts)
{
	struct ec_response_motion_sensor_data vect;
	struct motion_sensor_t *sensor;
	uint8_t tag;
	int id;
	int *axis;
	uint8_t *ptr;
	uint8_t ag_maps[] = {
		MOTIONSENSE_TYPE_GYRO,
		MOTIONSENSE_TYPE_ACCEL,
	};

	/*
	 * FIFO pattern is as follow (i.e. Acc/Gyro @ same ODR)
	 *  ________ ____________ _______ ____________
	 * | TAG_XL | Acc[x,y,z] | TAG_G | Gyr[x,y,z] |
	 * |________|____________|_______|____________|
	 * |<-------- 1 -------->|<-------- 2 ------->| (FIFO Threshold)
	 *
	 * First byte is tag, next data.
	 * Data pattern len is fixed for each sample.
	 * FIFO threshold is related to sample data (7 byte).
	 */
	ptr = fifo + LSM6DSO_TAG_SIZE;
	tag = (*fifo >> 3) - LSM6DSO_GYRO_TAG;
	id = ag_maps[tag];

	/* Discard samples every ODR changes. */
	if (samples_to_discard[id] > 0) {
		samples_to_discard[id]--;
		return;
	}

	sensor = main_s + id;
	axis = sensor->raw_xyz;

	/* Apply precision, sensitivity and rotation. */
	st_normalize(sensor, axis, ptr);
	vect.data[X] = axis[X];
	vect.data[Y] = axis[Y];
	vect.data[Z] = axis[Z];

	vect.flags = 0;
	vect.sensor_num = sensor - motion_sensors;
	motion_sense_fifo_stage_data(&vect, sensor, 3, saved_ts);
}

static inline int load_fifo(struct motion_sensor_t *s,
			    const struct lsm6dso_fstatus *fsts,
			    uint32_t saved_ts)
{
	uint8_t fifo[FIFO_READ_LEN], *ptr;
	int i, err, read_len = 0, word_len, fifo_len;
	uint16_t fifo_depth;

	fifo_depth = fsts->len & LSM6DSO_FIFO_DIFF_MASK;
	fifo_len = fifo_depth * LSM6DSO_FIFO_SAMPLE_SIZE;
	while (read_len < fifo_len) {
		word_len = GENERIC_MIN(fifo_len - read_len, sizeof(fifo));
		err = st_raw_read_n_noinc(s->port, s->i2c_spi_addr_flags,
					  LSM6DSO_FIFO_DATA_ADDR_TAG,
					  fifo, word_len);
		if (err != EC_SUCCESS)
			return err;

		for (i = 0; i < word_len; i += LSM6DSO_FIFO_SAMPLE_SIZE) {
			ptr = &fifo[i];
			push_fifo_data(LSM6DSO_MAIN_SENSOR(s), ptr, saved_ts);
		}
		read_len += word_len;
	}

	return read_len;
}

/**
 * accelgyro_config_fifo - update mode and ODR for FIFO decimator
 */
static int accelgyro_config_fifo(const struct motion_sensor_t *s)
{
	int err;
	struct stprivate_data *data = LSM6DSO_GET_DATA(s);
	uint8_t reg_val;
	uint8_t fifo_odr_mask;

	/* Changing in ODR must stop FIFO. */
	err = fifo_disable(s);
	if (err != EC_SUCCESS)
		return err;

	/*
	 * If ODR changes restore to default discard samples number
	 * the counter related to this sensor.
	 */
	samples_to_discard[s->type] = LSM6DSO_DISCARD_SAMPLES;

	fifo_odr_mask = LSM6DSO_FIFO_ODR_TO_REG(s);
	reg_val = LSM6DSO_ODR_TO_REG(data->base.odr);
	err = st_write_data_with_mask(s, LSM6DSO_FIFO_CTRL3_ADDR,
				      fifo_odr_mask, reg_val);
	if (err != EC_SUCCESS)
		return err;

	return fifo_enable(s);
}

/**
 * lsm6dso_interrupt - interrupt from int1 pin of sensor
 */
void lsm6dso_interrupt(enum gpio_signal signal)
{
	if (IS_ENABLED(CONFIG_ACCEL_FIFO))
		last_interrupt_timestamp = __hw_clock_source_read();

	task_set_event(TASK_ID_MOTIONSENSE,
		       CONFIG_ACCEL_LSM6DSO_INT_EVENT, 0);
}

/**
 * irq_handler - bottom half of the interrupt task sheduled by consumer
 */
static int irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	int ret = EC_SUCCESS;
	struct lsm6dso_fstatus fsts;

	if (((s->type != MOTIONSENSE_TYPE_ACCEL) &&
	     (s->type != MOTIONSENSE_TYPE_GYRO)) ||
	    (!(*event & CONFIG_ACCEL_LSM6DSO_INT_EVENT)))
		return EC_ERROR_NOT_HANDLED;

	if (IS_ENABLED(CONFIG_ACCEL_FIFO)) {
		/* Read how many data patterns on FIFO to read. */
		ret = st_raw_read_n_noinc(s->port, s->i2c_spi_addr_flags,
					  LSM6DSO_FIFO_STS1_ADDR,
					  (uint8_t *)&fsts, sizeof(fsts));
		if (ret != EC_SUCCESS)
			return ret;

		if (fsts.len & (LSM6DSO_FIFO_DATA_OVR | LSM6DSO_FIFO_FULL))
			CPRINTS("%s FIFO Overrun: %04x", s->name, fsts.len);

		if (fsts.len & LSM6DSO_FIFO_DIFF_MASK)
			ret = load_fifo(s, &fsts, last_interrupt_timestamp);

		if (IS_ENABLED(CONFIG_ACCEL_FIFO) && ret > 0)
			motion_sense_fifo_commit_data();
	}

	return ret;
}
#endif /* CONFIG_ACCEL_INTERRUPTS */

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
	struct stprivate_data *data = LSM6DSO_GET_DATA(s);
	int newrange = range;

	ctrl_reg = LSM6DSO_RANGE_REG(s->type);
	if (s->type == MOTIONSENSE_TYPE_ACCEL) {
		/* Adjust and check rounded value for Acc. */
		if (rnd && (newrange < LSM6DSO_ACCEL_NORMALIZE_FS(newrange)))
			newrange *= 2;

		if (newrange > LSM6DSO_ACCEL_FS_MAX_VAL)
			newrange = LSM6DSO_ACCEL_FS_MAX_VAL;

		reg_val = lsm6dso_accel_fs_reg(newrange);
	} else {
		/* Adjust and check rounded value for Gyro. */
		reg_val = LSM6DSO_GYRO_FS_REG(range);
		if (rnd && (range > LSM6DSO_GYRO_NORMALIZE_FS(reg_val)))
			reg_val++;

		if (reg_val > LSM6DSO_GYRO_FS_MAX_REG_VAL)
			reg_val = LSM6DSO_GYRO_FS_MAX_REG_VAL;

		newrange = LSM6DSO_GYRO_NORMALIZE_FS(reg_val);
	}

	mutex_lock(s->mutex);
	err = st_write_data_with_mask(s, ctrl_reg, LSM6DSO_RANGE_MASK,
				      reg_val);
	if (err == EC_SUCCESS)
		data->base.range = newrange;

	mutex_unlock(s->mutex);

	return EC_SUCCESS;
}

/**
 * get_range - get full scale range
 * @s: Motion sensor pointer
 */
static int get_range(const struct motion_sensor_t *s)
{
	struct stprivate_data *data = LSM6DSO_GET_DATA(s);

	return data->base.range;
}

/**
 * set_data_rate set sensor data rate
 * @s: Motion sensor pointer
 * @range: Rate (mHz)
 * @rnd: Round up/down flag
 */
static int set_data_rate(const struct motion_sensor_t *s, int rate, int rnd)
{
	int ret, normalized_rate = 0;
	struct stprivate_data *data = LSM6DSO_GET_DATA(s);
	uint8_t ctrl_reg, reg_val = 0;

	ctrl_reg = LSM6DSO_ODR_REG(s->type);
	if (rate > 0) {
		reg_val = LSM6DSO_ODR_TO_REG(rate);
		normalized_rate = LSM6DSO_REG_TO_ODR(reg_val);

		if (rnd && (normalized_rate < rate)) {
			reg_val++;
			normalized_rate = LSM6DSO_REG_TO_ODR(reg_val);
		}

		if (normalized_rate < LSM6DSO_ODR_MIN_VAL ||
		    normalized_rate > MIN(LSM6DSO_ODR_MAX_VAL,
					  CONFIG_EC_MAX_SENSOR_FREQ_MILLIHZ))
			return EC_RES_INVALID_PARAM;
	}

	mutex_lock(s->mutex);
	ret = st_write_data_with_mask(s, ctrl_reg, LSM6DSO_ODR_MASK, reg_val);
	if (ret == EC_SUCCESS) {
		data->base.odr = normalized_rate;
		if (IS_ENABLED(CONFIG_ACCEL_FIFO))
			accelgyro_config_fifo(s);
	}

	mutex_unlock(s->mutex);

	return ret;
}

static int is_data_ready(const struct motion_sensor_t *s, int *ready)
{
	int ret, tmp;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags,
			   LSM6DSO_STATUS_REG, &tmp);
	if (ret != EC_SUCCESS) {
		CPRINTS("%s type:0x%X RS Error", s->name, s->type);

		return ret;
	}

	if (MOTIONSENSE_TYPE_ACCEL == s->type)
		*ready = (LSM6DSO_STS_XLDA_UP == (tmp & LSM6DSO_STS_XLDA_MASK));
	else
		*ready = (LSM6DSO_STS_GDA_UP == (tmp & LSM6DSO_STS_GDA_MASK));

	return EC_SUCCESS;
}

/*
 * Is not very efficient to collect the data in read: better have an interrupt
 * and collect in FIFO, even if it has one item: we don't have to check if the
 * sensor is ready (minimize I2C access).
 */
static int read(const struct motion_sensor_t *s, intv3_t v)
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
	ret = st_raw_read_n_noinc(s->port, s->i2c_spi_addr_flags,
				  xyz_reg, raw, OUT_XYZ_SIZE);
	if (ret != EC_SUCCESS)
		return ret;

	/* Apply precision, sensitivity and rotation vector. */
	st_normalize(s, v, raw);

	return EC_SUCCESS;
}

static int init(const struct motion_sensor_t *s)
{
	int ret = 0, tmp;
	struct stprivate_data *data = LSM6DSO_GET_DATA(s);

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags,
			   LSM6DSO_WHO_AM_I_REG, &tmp);
	if (ret != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	if (tmp != LSM6DSO_WHO_AM_I)
		return EC_ERROR_ACCESS_DENIED;

	/*
	 * This sensor can be powered through an EC reboot, so the state of the
	 * sensor is unknown here so reset it
	 * LSM6DSO supports both Acc & Gyro features
	 * Board will see two virtual sensor devices: Acc & Gyro
	 * Requirement: Acc need be init before Gyro
	 */
	if (s->type == MOTIONSENSE_TYPE_ACCEL) {
		mutex_lock(s->mutex);

		/* Software reset. */
		ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
				    LSM6DSO_CTRL3_ADDR, LSM6DSO_SW_RESET);
		if (ret != EC_SUCCESS)
			goto err_unlock;

		/*
		 * Output data not updated until have been read.
		 * Prefer interrupt to be active low.
		 */
		ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
				    LSM6DSO_CTRL3_ADDR,
				    LSM6DSO_BDU | LSM6DSO_IF_INC);
		if (ret != EC_SUCCESS)
			goto err_unlock;

		if (IS_ENABLED(CONFIG_ACCEL_FIFO)) {
			ret = fifo_disable(s);
			if (ret != EC_SUCCESS)
				goto err_unlock;
		}

#ifdef CONFIG_ACCEL_INTERRUPTS
		ret = config_interrupt(s);
		if (ret != EC_SUCCESS)
			goto err_unlock;
#endif /* CONFIG_ACCEL_INTERRUPTS */

		mutex_unlock(s->mutex);
	}

	/* Set default resolution common to Acc and Gyro. */
	data->resol = LSM6DSO_RESOLUTION;
	return sensor_init_done(s);

err_unlock:
	mutex_unlock(s->mutex);
	CPRINTS("%s: MS Init type:0x%X Error", s->name, s->type);

	return ret;
}

const struct accelgyro_drv lsm6dso_drv = {
	.init = init,
	.read = read,
	.set_range = set_range,
	.get_range = get_range,
	.get_resolution = st_get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = st_get_data_rate,
	.set_offset = st_set_offset,
	.get_offset = st_get_offset,
#ifdef CONFIG_ACCEL_INTERRUPTS
	.irq_handler = irq_handler,
#endif /* CONFIG_ACCEL_INTERRUPTS */
};
