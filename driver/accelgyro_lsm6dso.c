/* Copyright 2019 The ChromiumOS Authors
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

#ifdef CONFIG_ACCEL_LSM6DSO_INT_EVENT
#define ACCEL_LSM6DSO_INT_ENABLE
#endif

#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ##args)

STATIC_IF(ACCEL_LSM6DSO_INT_ENABLE)
volatile uint32_t last_interrupt_timestamp;

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

#ifdef ACCEL_LSM6DSO_INT_ENABLE
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
	if (IS_ENABLED(CONFIG_ACCEL_SPOOF_MODE) &&
	    sensor->flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE)
		axis = sensor->spoof_xyz;
	if (IS_ENABLED(CONFIG_ACCEL_FIFO)) {
		struct ec_response_motion_sensor_data vect;

		vect.data[X] = axis[X];
		vect.data[Y] = axis[Y];
		vect.data[Z] = axis[Z];

		vect.flags = 0;
		vect.sensor_num = sensor - motion_sensors;
		motion_sense_fifo_stage_data(&vect, sensor, 3, saved_ts);
	} else {
		motion_sense_push_raw_xyz(sensor);
	}
}

static inline int load_fifo(struct motion_sensor_t *main_s,
			    const uint16_t fifo_len)
{
	uint8_t fifo[LSM6DSO_FIFO_SAMPLE_SIZE];
	int i;

	for (i = 0; i < fifo_len; i++) {
		RETURN_ERROR(st_raw_read_n_noinc(
			main_s->port, main_s->i2c_spi_addr_flags,
			LSM6DSO_FIFO_DATA_ADDR_TAG, fifo, sizeof(fifo)));

		push_fifo_data(main_s, fifo, last_interrupt_timestamp);
	}

	return EC_SUCCESS;
}

/**
 * accelgyro_config_fifo - update mode and ODR for FIFO decimator
 */
static int accelgyro_config_fifo(const struct motion_sensor_t *s)
{
	int err;
	struct stprivate_data *data = s->drv_data;
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

	fifo_odr_mask = LSM6DSO_FIFO_ODR_MASK(s);
	reg_val = LSM6DSO_ODR_TO_REG(data->base.odr);
	err = st_write_data_with_mask(s, LSM6DSO_FIFO_CTRL3_ADDR, fifo_odr_mask,
				      reg_val);
	if (err != EC_SUCCESS)
		return err;

	return fifo_enable(s);
}

/**
 * lsm6dso_interrupt - interrupt from int1 pin of sensor
 */
test_mockable void lsm6dso_interrupt(enum gpio_signal signal)
{
	last_interrupt_timestamp = __hw_clock_source_read();

	task_set_event(TASK_ID_MOTIONSENSE, CONFIG_ACCEL_LSM6DSO_INT_EVENT);
}

/**
 * irq_handler - bottom half of the interrupt task sheduled by consumer
 */
static int irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	int fifo_len = 0;
	struct lsm6dso_fstatus fsts;
	bool has_read_fifo = false;

	if ((s->type != MOTIONSENSE_TYPE_ACCEL) ||
	    (!(*event & CONFIG_ACCEL_LSM6DSO_INT_EVENT)))
		return EC_ERROR_NOT_HANDLED;

	do {
		/* Read how many data patterns on FIFO to read. */
		RETURN_ERROR(st_raw_read_n_noinc(
			s->port, s->i2c_spi_addr_flags, LSM6DSO_FIFO_STS1_ADDR,
			(uint8_t *)&fsts, sizeof(fsts)));
		if (fsts.len & (LSM6DSO_FIFO_DATA_OVR | LSM6DSO_FIFO_FULL))
			CPRINTS("%s FIFO Overrun: %04x", s->name, fsts.len);

		fifo_len = fsts.len & LSM6DSO_FIFO_DIFF_MASK;
		if (fifo_len) {
			RETURN_ERROR(load_fifo(s, fifo_len));
			has_read_fifo = true;
		}
	} while (fifo_len != 0);

	if (IS_ENABLED(CONFIG_ACCEL_FIFO) && has_read_fifo)
		motion_sense_fifo_commit_data();

	return EC_SUCCESS;
}
#endif /* ACCEL_LSM6DSO_INT_ENABLE */

/**
 * set_range - set full scale range
 * @s: Motion sensor pointer
 * @range: Range
 * @rnd: Round up/down flag
 * Note: Range is sensitivity/gain for speed purpose
 */
static int set_range(struct motion_sensor_t *s, int range, int rnd)
{
	int err;
	uint8_t ctrl_reg, reg_val;
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
	err = st_write_data_with_mask(s, ctrl_reg, LSM6DSO_RANGE_MASK, reg_val);
	if (err == EC_SUCCESS)
		s->current_range = newrange;

	mutex_unlock(s->mutex);

	return EC_SUCCESS;
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
	struct stprivate_data *data = s->drv_data;
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
		    normalized_rate > LSM6DSO_ODR_MAX_VAL)
			return EC_RES_INVALID_PARAM;
	}

	mutex_lock(s->mutex);
	ret = st_write_data_with_mask(s, ctrl_reg, LSM6DSO_ODR_MASK, reg_val);
	if (ret == EC_SUCCESS) {
		data->base.odr = normalized_rate;
		if (IS_ENABLED(ACCEL_LSM6DSO_INT_ENABLE))
			accelgyro_config_fifo(s);
	}

	mutex_unlock(s->mutex);

	return ret;
}

static int is_data_ready(const struct motion_sensor_t *s, int *ready)
{
	int ret, tmp;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags, LSM6DSO_STATUS_REG,
			   &tmp);
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
	ret = st_raw_read_n_noinc(s->port, s->i2c_spi_addr_flags, xyz_reg, raw,
				  OUT_XYZ_SIZE);
	if (ret != EC_SUCCESS)
		return ret;

	/* Apply precision, sensitivity and rotation vector. */
	st_normalize(s, v, raw);

	return EC_SUCCESS;
}

static int init(struct motion_sensor_t *s)
{
	int ret = 0, tmp;
	struct stprivate_data *data = s->drv_data;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags, LSM6DSO_WHO_AM_I_REG,
			   &tmp);
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
		 * Require interrupt to be active low.
		 */
		ret = st_raw_write8(
			s->port, s->i2c_spi_addr_flags, LSM6DSO_CTRL3_ADDR,
			LSM6DSO_BDU | LSM6DSO_IF_INC | LSM6DSO_H_L_ACTIVE);
		if (ret != EC_SUCCESS)
			goto err_unlock;

		if (IS_ENABLED(ACCEL_LSM6DSO_INT_ENABLE)) {
			ret = fifo_disable(s);
			if (ret != EC_SUCCESS)
				goto err_unlock;
		}

		if (IS_ENABLED(ACCEL_LSM6DSO_INT_ENABLE))
			ret = config_interrupt(s);
		if (ret != EC_SUCCESS)
			goto err_unlock;

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

#ifdef CONFIG_BODY_DETECTION
static int get_rms_noise(const struct motion_sensor_t *s)
{
	/*
	 * RMS | Acceleration RMS noise in normal/low-power mode
	 * FS = ±4 g | 2.0 mg(RMS)
	 */
	return 2000;
}
#endif

const struct accelgyro_drv lsm6dso_drv = {
	.init = init,
	.read = read,
	.set_range = set_range,
	.get_resolution = st_get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = st_get_data_rate,
	.set_offset = st_set_offset,
	.get_offset = st_get_offset,
#ifdef ACCEL_LSM6DSO_INT_ENABLE
	.irq_handler = irq_handler,
#ifdef CONFIG_BODY_DETECTION
	.get_rms_noise = get_rms_noise,
#endif
#ifdef CONFIG_GESTURE_HOST_DETECTION
	.list_activities = st_list_activities,
#endif
#endif /* ACCEL_LSM6DSO_INT_ENABLE */
};
