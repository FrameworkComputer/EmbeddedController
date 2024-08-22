/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * LSM6DSx (x is L/M/3) accelerometer and gyro module for Chrome EC
 * 3D digital accelerometer & 3D digital gyroscope
 * This driver supports both devices LSM6DSM and LSM6DSL
 */

#include "builtin/assert.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "driver/mag_lis2mdl.h"
#include "hooks.h"
#include "hwtimer.h"
#include "mag_cal.h"
#include "math_util.h"
#include "motion_sense_fifo.h"
#include "queue.h"
#include "task.h"
#include "timer.h"

#ifdef CONFIG_ACCEL_LSM6DSM_INT_EVENT
#define ACCEL_LSM6DSM_INT_ENABLE
#endif

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ##args)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ##args)

STATIC_IF(ACCEL_LSM6DSM_INT_ENABLE)
volatile uint32_t last_interrupt_timestamp;

/**
 * Gets the sensor type associated with the dev_fifo enum. This type can be used
 * to get the sensor number by using it as an offset from the first sensor in
 * the lsm6dsm (the accelerometer).
 *
 * @param fifo_type The dev_fifo enum in question.
 * @return the type of sensor represented by the fifo type.
 */
static inline uint8_t get_sensor_type(enum dev_fifo fifo_type)
{
	static uint8_t map[] = {
		MOTIONSENSE_TYPE_GYRO,
		MOTIONSENSE_TYPE_ACCEL,
		MOTIONSENSE_TYPE_MAG,
	};
	return map[fifo_type];
}

/**
 * @return output base register for sensor
 */
static inline int get_xyz_reg(enum motionsensor_type type)
{
	return LSM6DSM_ACCEL_OUT_X_L_ADDR -
	       (LSM6DSM_ACCEL_OUT_X_L_ADDR - LSM6DSM_GYRO_OUT_X_L_ADDR) * type;
}

/**
 * Configure interrupt int 1 to fire handler for:
 *
 * FIFO threshold on watermark
 *
 * @accel: Motion sensor pointer to accelerometer.
 */
__maybe_unused static int config_interrupt(const struct motion_sensor_t *accel)
{
	int int1_ctrl_val;

	RETURN_ERROR(st_raw_read8(accel->port, accel->i2c_spi_addr_flags,
				  LSM6DSM_INT1_CTRL, &int1_ctrl_val));

	/* As soon as one sample is ready, trigger an interrupt. */
	RETURN_ERROR(st_raw_write8(accel->port, accel->i2c_spi_addr_flags,
				   LSM6DSM_FIFO_CTRL1_ADDR,
				   OUT_XYZ_SIZE / sizeof(uint16_t)));
	int1_ctrl_val |= LSM6DSM_INT_FIFO_TH | LSM6DSM_INT_FIFO_OVR |
			 LSM6DSM_INT_FIFO_FULL;

	return st_raw_write8(accel->port, accel->i2c_spi_addr_flags,
			     LSM6DSM_INT1_CTRL, int1_ctrl_val);
}

/**
 * fifo_disable - set fifo mode
 * @accel: Motion sensor pointer: must be MOTIONSENSE_TYPE_ACCEL.
 * @fmode: BYPASS or CONTINUOUS
 */
static int fifo_disable(const struct motion_sensor_t *accel)
{
	return st_raw_write8(accel->port, accel->i2c_spi_addr_flags,
			     LSM6DSM_FIFO_CTRL5_ADDR, 0x00);
}

/**
 * fifo_reset_pattern: called at each new FIFO pattern.
 */
static void fifo_reset_pattern(struct lsm6dsm_data *private)
{
	/* The fifo is ready to run. */
	memcpy(&private->accel_fifo_state->current,
	       &private->accel_fifo_state->config,
	       sizeof(struct lsm6dsm_fifo_data));
	private->accel_fifo_state->next_in_pattern = FIFO_DEV_INVALID;
}

/**
 * fifo_enable - Configure internal FIFO parameters
 * @accel must be the accelerometer sensor.
 *
 * Configure FIFO decimators to have every time the right pattern
 * with acc/gyro
 */
static int fifo_enable(const struct motion_sensor_t *accel)
{
	const struct motion_sensor_t *s;
	int err, i, rate;
	uint8_t decimators[FIFO_DEV_NUM] = { 0 };
	unsigned int odrs[FIFO_DEV_NUM];
	unsigned int min_odr = LSM6DSM_ODR_MAX_VAL;
	unsigned int max_odr = 0;
	uint8_t odr_reg_val;
	struct lsm6dsm_data *private = LSM6DSM_GET_DATA(accel);
	struct lsm6dsm_accel_fifo_state *fifo_state = private->accel_fifo_state;
	/* In FIFO sensors are mapped in a different way. */
	uint8_t agm_maps[] = {
		MOTIONSENSE_TYPE_GYRO,
		MOTIONSENSE_TYPE_ACCEL,
		MOTIONSENSE_TYPE_MAG,
	};

	/* Search for min and max odr values for acc, gyro. */
	for (i = FIFO_DEV_GYRO; i < FIFO_DEV_NUM; i++) {
		/* Check if sensor enabled with ODR. */
		s = accel + agm_maps[i];
		rate = s->drv->get_data_rate(s);
		if (rate > 0) {
			min_odr = MIN(min_odr, rate);
			max_odr = MAX(max_odr, rate);
		}
		odrs[i] = rate;
	}

	if (max_odr == 0) {
		/* Leave FIFO disabled. */
		return EC_SUCCESS;
	}

	/* FIFO ODR must be set before the decimation factors */
	odr_reg_val = LSM6DSM_ODR_TO_REG(max_odr) << LSM6DSM_FIFO_CTRL5_ODR_OFF;
	err = st_raw_write8(accel->port, accel->i2c_spi_addr_flags,
			    LSM6DSM_FIFO_CTRL5_ADDR, odr_reg_val);

	/* Scan all sensors configuration to calculate FIFO decimator. */
	fifo_state->config.total_samples_in_pattern = 0;
	for (i = FIFO_DEV_GYRO; i < FIFO_DEV_NUM; i++) {
		if (odrs[i] > 0) {
			fifo_state->config.samples_in_pattern[i] =
				odrs[i] / min_odr;
			decimators[i] =
				LSM6DSM_FIFO_DECIMATOR(max_odr / odrs[i]);
			fifo_state->config.total_samples_in_pattern +=
				fifo_state->config.samples_in_pattern[i];
		} else {
			/* Not in FIFO if sensor disabled. */
			fifo_state->config.samples_in_pattern[i] = 0;
		}
	}
	st_raw_write8(accel->port, accel->i2c_spi_addr_flags,
		      LSM6DSM_FIFO_CTRL3_ADDR,
		      (decimators[FIFO_DEV_GYRO] << LSM6DSM_FIFO_DEC_G_OFF) |
			      (decimators[FIFO_DEV_ACCEL]
			       << LSM6DSM_FIFO_DEC_XL_OFF));
	/* b/255967867: Cannot use IS_ENABLED here with clang. */
#ifdef CONFIG_LSM6DSM_SEC_I2C
	ASSERT(ARRAY_SIZE(decimators) > FIFO_DEV_MAG);
	st_raw_write8(accel->port, accel->i2c_spi_addr_flags,
		      LSM6DSM_FIFO_CTRL4_ADDR, decimators[FIFO_DEV_MAG]);

	/*
	 * FIFO ODR is limited by odr of gyro or accel.
	 * If we are sampling magnetometer faster than gyro or accel,
	 * bump up ODR of accel. Thanks to decimation we will still
	 * measure at the specified ODR.
	 * Contrary to gyroscope, sampling faster will not affect
	 * measurements.
	 * Set the ODR behind the back of set/get_data_rate.
	 *
	 * First samples after ODR changes must be thrown out [See
	 * AN4987, section 3.9].
	 * When increasing accel ODR, the FIFO is going to drop samples,
	 * - except the first one after ODR change.
	 * When decreasing accel ODR, we don't need to drop sample if
	 * frequency is less than 52Hz.
	 * At most, we need to drop one sample, but Android requirement
	 * specify that changing one sensor ODR should not affect other
	 * sensors.
	 * Leave the bad sample alone, it will be a single glitch in the
	 * accelerometer data stream.
	 */
	if (max_odr > MAX(odrs[FIFO_DEV_ACCEL], odrs[FIFO_DEV_GYRO])) {
		st_write_data_with_mask(accel, LSM6DSM_ODR_REG(accel->type),
					LSM6DSM_ODR_MASK,
					LSM6DSM_ODR_TO_REG(max_odr));
	} else {
		st_write_data_with_mask(
			accel, LSM6DSM_ODR_REG(accel->type), LSM6DSM_ODR_MASK,
			LSM6DSM_ODR_TO_REG(odrs[FIFO_DEV_ACCEL]));
	}
#endif
	/*
	 * After ODR and decimation values are set, continuous mode can be
	 * enabled
	 */
	err = st_raw_write8(accel->port, accel->i2c_spi_addr_flags,
			    LSM6DSM_FIFO_CTRL5_ADDR,
			    odr_reg_val | LSM6DSM_FIFO_MODE_CONTINUOUS_VAL);
	if (err != EC_SUCCESS)
		return err;
	fifo_reset_pattern(private);
	return EC_SUCCESS;
}

#ifdef ACCEL_LSM6DSM_INT_ENABLE
/*
 * Must order FIFO read based on ODR:
 * For example Acc @ 52 Hz, Gyro @ 26 Hz Mag @ 13 Hz in FIFO we have
 * for each pattern this data samples:
 *  ________ _______ _______ _______ ________ _______ _______
 * | Gyro_0 | Acc_0 | Mag_0 | Acc_1 | Gyro_1 | Acc_2 | Acc_3 |
 * |________|_______|_______|_______|________|_______|_______|
 *
 * Total samples for each pattern: 2 Gyro, 4 Acc, 1 Mag.
 *
 * Returns dev_fifo enum value of next sample to process
 */
static int fifo_next(struct lsm6dsm_data *private)
{
	int next_id;
	struct lsm6dsm_accel_fifo_state *fifo_state = private->accel_fifo_state;

	if (fifo_state->current.total_samples_in_pattern == 0)
		fifo_reset_pattern(private);

	if (fifo_state->current.total_samples_in_pattern == 0) {
		/*
		 * Not expected we are supposed to be called to process FIFO
		 * data.
		 */
		CPRINTS("FIFO empty pattern");
		return FIFO_DEV_INVALID;
	}

	for (next_id = fifo_state->next_in_pattern + 1; 1; next_id++) {
		if (next_id == FIFO_DEV_NUM)
			next_id = FIFO_DEV_GYRO;
		if (fifo_state->current.samples_in_pattern[next_id] != 0) {
			fifo_state->current.samples_in_pattern[next_id]--;
			fifo_state->current.total_samples_in_pattern--;
			fifo_state->next_in_pattern = next_id;
			return next_id;
		}
	}
	/* Will never happen. */
	return FIFO_DEV_INVALID;
}

/**
 * push_fifo_data - Scan data pattern and push upside
 */
static void push_fifo_data(struct motion_sensor_t *accel, uint8_t *fifo,
			   uint16_t flen, uint32_t timestamp)
{
	struct motion_sensor_t *s;
	struct lsm6dsm_data *private = LSM6DSM_GET_DATA(accel);

	while (flen > 0) {
		int id;
		int *axis;
		int next_fifo = fifo_next(private);
		/*
		 * This should never happen, but it could. There will be a
		 * report from inside fifo_next about it, so no extra message
		 * required here.
		 */
		if (next_fifo == FIFO_DEV_INVALID) {
			return;
		}

		id = get_sensor_type(next_fifo);
		if (private->accel_fifo_state->samples_to_discard[id] > 0) {
			private->accel_fifo_state->samples_to_discard[id]--;
		} else {
			s = accel + id;
			axis = s->raw_xyz;

			/* Apply precision, sensitivity and rotation. */
			if (IS_ENABLED(CONFIG_MAG_LSM6DSM_LIS2MDL) &&
			    (s->type == MOTIONSENSE_TYPE_MAG)) {
				lis2mdl_normalize(s, axis, fifo);
				rotate(axis, *s->rot_standard_ref, axis);
			} else {
				st_normalize(s, axis, fifo);
			}

			if (IS_ENABLED(CONFIG_ACCEL_SPOOF_MODE) &&
			    s->flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE)
				axis = s->spoof_xyz;
			if (IS_ENABLED(CONFIG_ACCEL_FIFO)) {
				struct ec_response_motion_sensor_data vect;

				vect.data[X] = axis[X];
				vect.data[Y] = axis[Y];
				vect.data[Z] = axis[Z];

				vect.flags = 0;
				vect.sensor_num = s - motion_sensors;
				motion_sense_fifo_stage_data(&vect, s, 3,
							     timestamp);
			} else {
				motion_sense_push_raw_xyz(s);
			}
		}

		fifo += OUT_XYZ_SIZE;
		flen -= OUT_XYZ_SIZE;
	}
}

static int load_fifo(struct motion_sensor_t *s, const struct fstatus *fsts)
{
	uint32_t interrupt_timestamp = last_interrupt_timestamp;
	int err, left, length;
	uint8_t fifo[FIFO_READ_LEN];

	/*
	 * DIFF[11:0] are number of unread uint16 in FIFO
	 * mask DIFF and compute total byte len to read from FIFO.
	 */
	left = fsts->len & LSM6DSM_FIFO_DIFF_MASK;
	left *= sizeof(uint16_t);
	left = (left / OUT_XYZ_SIZE) * OUT_XYZ_SIZE;

	/* Push all data on upper side. */
	do {
		/* Fit len to pre-allocated static buffer. */
		if (left > FIFO_READ_LEN)
			length = FIFO_READ_LEN;
		else
			length = left;

		/* Read data and copy in buffer. */
		err = st_raw_read_n_noinc(s->port, s->i2c_spi_addr_flags,
					  LSM6DSM_FIFO_DATA_ADDR, fifo, length);
		if (err != EC_SUCCESS)
			return err;

		/*
		 * Manage patterns and push data. Data is pushed with the
		 * timestamp of the interrupt that got us into this function
		 * in the first place. This avoids a potential race condition
		 * where we empty the FIFO, and a new IRQ comes in between
		 * reading the last sample and pushing it into the FIFO.
		 */
		push_fifo_data(s, fifo, length, interrupt_timestamp);
		left -= length;
	} while (left > 0);

	return EC_SUCCESS;
}

/**
 * lsm6dsm_interrupt - interrupt from int1/2 pin of sensor
 */
test_mockable void lsm6dsm_interrupt(enum gpio_signal signal)
{
	last_interrupt_timestamp = __hw_clock_source_read();

	task_set_event(TASK_ID_MOTIONSENSE, CONFIG_ACCEL_LSM6DSM_INT_EVENT);
}

/**
 * irq_handler - bottom half of the interrupt stack
 */
static int irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	struct fstatus fsts;
	int fifo_empty = false;
	bool commit_needed = false;

	if ((s->type != MOTIONSENSE_TYPE_ACCEL) ||
	    (!(*event & CONFIG_ACCEL_LSM6DSM_INT_EVENT)))
		return EC_ERROR_NOT_HANDLED;

	while (!fifo_empty) {
		/* Read how many data pattern on FIFO to read. */
		RETURN_ERROR(st_raw_read_n_noinc(
			s->port, s->i2c_spi_addr_flags, LSM6DSM_FIFO_STS1_ADDR,
			(uint8_t *)&fsts, sizeof(fsts)));
		if (fsts.len & (LSM6DSM_FIFO_DATA_OVR | LSM6DSM_FIFO_FULL))
			CPRINTS("%s FIFO Overrun: %04x", s->name, fsts.len);
		fifo_empty = fsts.len & LSM6DSM_FIFO_EMPTY;
		if (!fifo_empty) {
			commit_needed = true;
			RETURN_ERROR(load_fifo(s, &fsts));
		}
	}
	if (IS_ENABLED(CONFIG_ACCEL_FIFO) && commit_needed)
		motion_sense_fifo_commit_data();

	return EC_SUCCESS;
}
#endif /* ACCEL_LSM6DSM_INT_ENABLE */

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

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		/* Adjust and check rounded value for acc. */
		if (rnd && (newrange < LSM6DSM_ACCEL_NORMALIZE_FS(newrange)))
			newrange *= 2;

		if (newrange > LSM6DSM_ACCEL_FS_MAX_VAL)
			newrange = LSM6DSM_ACCEL_FS_MAX_VAL;

		reg_val = LSM6DSM_ACCEL_FS_REG(newrange);
		break;
	case MOTIONSENSE_TYPE_GYRO:
		/* Adjust and check rounded value for gyro. */
		reg_val = LSM6DSM_GYRO_FS_REG(range);
		if (rnd && (range > LSM6DSM_GYRO_NORMALIZE_FS(reg_val)))
			reg_val++;

		if (reg_val > LSM6DSM_GYRO_FS_MAX_REG_VAL)
			reg_val = LSM6DSM_GYRO_FS_MAX_REG_VAL;
		newrange = LSM6DSM_GYRO_NORMALIZE_FS(reg_val);
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	ctrl_reg = LSM6DSM_RANGE_REG(s->type);
	mutex_lock(s->mutex);
	err = st_write_data_with_mask(s, ctrl_reg, LSM6DSM_RANGE_MASK, reg_val);
	if (err == EC_SUCCESS)
		/* Save internally gain for speed optimization. */
		s->current_range = newrange;
	mutex_unlock(s->mutex);
	return err;
}

/**
 * lsm6dsm_set_data_rate
 * @s: Motion sensor pointer
 * @range: Rate (mHz)
 * @rnd: Round up/down flag
 *
 * For mag in cascade with lsm6dsm/l we use acc trigger and FIFO decimators
 */
int lsm6dsm_set_data_rate(const struct motion_sensor_t *s, int rate, int rnd)
{
	struct stprivate_data *data = s->drv_data;
	const struct motion_sensor_t *accel = LSM6DSM_MAIN_SENSOR(s);
	struct lsm6dsm_data *private = LSM6DSM_GET_DATA(accel);
	int ret = EC_SUCCESS, normalized_rate = 0;
	uint8_t ctrl_reg, reg_val = 0;

	/* FIFO must be disabled before setting any ODR values */
	ret = fifo_disable(accel);
	if (ret != EC_SUCCESS) {
		CPRINTS("Failed to disable FIFO. Error: %d", ret);
		return ret;
	}

	if (rate > 0) {
		reg_val = LSM6DSM_ODR_TO_REG(rate);
		normalized_rate = LSM6DSM_REG_TO_ODR(reg_val);

		if (rnd && (normalized_rate < rate)) {
			reg_val++;
			normalized_rate = LSM6DSM_REG_TO_ODR(reg_val);
		}
		if (normalized_rate < LSM6DSM_ODR_MIN_VAL ||
		    normalized_rate > LSM6DSM_ODR_MAX_VAL)
			return EC_RES_INVALID_PARAM;
	}

	/*
	 * TODO(b:110143516) Improve data rate selection:
	 * Sensor is always running at 100Hz, even when not used.
	 */
	if (IS_ENABLED(CONFIG_MAG_LSM6DSM_LIS2MDL) &&
	    (s->type == MOTIONSENSE_TYPE_MAG)) {
		struct mag_cal_t *cal = LIS2MDL_CAL(s);

		init_mag_cal(cal);
		/*
		 * Magnetometer ODR is calculating at 100Hz, but we are reading
		 * less often.
		 */
		if (normalized_rate > 0)
			cal->batch_size =
				MAX(MAG_CAL_MIN_BATCH_SIZE,
				    (normalized_rate * 1000) /
					    MAG_CAL_MIN_BATCH_WINDOW_US);
		else
			cal->batch_size = 0;
		CPRINTS("Batch size: %d", cal->batch_size);
		mutex_lock(s->mutex);
	} else {
		mutex_lock(s->mutex);
		ctrl_reg = LSM6DSM_ODR_REG(s->type);
		ret = st_write_data_with_mask(s, ctrl_reg, LSM6DSM_ODR_MASK,
					      reg_val);
	}
	if (ret == EC_SUCCESS) {
		struct lsm6dsm_accel_fifo_state *fifo_state =
			private->accel_fifo_state;

		data->base.odr = normalized_rate;
		fifo_state->samples_to_discard[s->type] =
			LSM6DSM_DISCARD_SAMPLES;
		ret = fifo_enable(accel);
		if (ret != EC_SUCCESS)
			CPRINTS("Failed to enable FIFO. Error: %d", ret);
	}

	mutex_unlock(s->mutex);
	return ret;
}

static int is_data_ready(const struct motion_sensor_t *s, int *ready)
{
	int ret, tmp;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags, LSM6DSM_STATUS_REG,
			   &tmp);
	if (ret != EC_SUCCESS) {
		CPRINTS("%s type:0x%X RS Error", s->name, s->type);
		return ret;
	}

	if (MOTIONSENSE_TYPE_ACCEL == s->type)
		*ready = (LSM6DSM_STS_XLDA_UP == (tmp & LSM6DSM_STS_XLDA_MASK));
	else
		*ready = (LSM6DSM_STS_GDA_UP == (tmp & LSM6DSM_STS_GDA_MASK));

	return EC_SUCCESS;
}

/*
 * Is not very efficient to collect the data in read: better have an interrupt
 * and collect the FIFO, even if it has one item: we don't have to check if the
 * sensor is ready (minimize I2C access).
 */
static int read(const struct motion_sensor_t *s, intv3_t v)
{
	uint8_t raw[OUT_XYZ_SIZE];
	uint8_t xyz_reg;
	int tmp = 0;

	RETURN_ERROR(is_data_ready(s, &tmp));

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
	RETURN_ERROR(st_raw_read_n_noinc(s->port, s->i2c_spi_addr_flags,
					 xyz_reg, raw, OUT_XYZ_SIZE));

	/* Apply precision, sensitivity and rotation vector. */
	st_normalize(s, v, raw);
	return EC_SUCCESS;
}

static int init(struct motion_sensor_t *s)
{
	int ret = 0, tmp;
	struct stprivate_data *data = s->drv_data;
	uint8_t ctrl_reg, reg_val = 0;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags, LSM6DSM_WHO_AM_I_REG,
			   &tmp);
	if (ret != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	if (tmp != LSM6DS3_WHO_AM_I && tmp != LSM6DSM_WHO_AM_I) {
		/* Unrecognized sensor */
		CPRINTS("Unknown WHO_AM_I value: 0x%x", tmp);
		return EC_ERROR_ACCESS_DENIED;
	} else {
		/*
		 * Recognized sensor
		 * Log print for factory check chip ID (b:196781249)
		 */
		CPRINTS("SENSOR accelgyro_lsm6dsm WHO_AM_I value: 0x%x", tmp);
	}

	/*
	 * This sensor can be powered through an EC reboot, so the state of the
	 * sensor is unknown here so reset it
	 * LSM6DSM/L supports both accel & gyro features
	 * Board will see two virtual sensor devices: accel & gyro
	 * Requirement: Accel need be init before gyro and mag
	 */
	if (s->type == MOTIONSENSE_TYPE_ACCEL) {
		mutex_lock(s->mutex);

		/* Software reset procedure. */
		reg_val = LSM6DSM_ODR_TO_REG(LSM6DSM_ODR_MIN_VAL);
		ctrl_reg = LSM6DSM_ODR_REG(MOTIONSENSE_TYPE_ACCEL);

		/* Power OFF gyro. */
		ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
				    LSM6DSM_CTRL2_ADDR, 0);
		if (ret != EC_SUCCESS)
			goto err_unlock;

		/* Power ON Accel. */
		ret = st_raw_write8(s->port, s->i2c_spi_addr_flags, ctrl_reg,
				    reg_val);
		if (ret != EC_SUCCESS)
			goto err_unlock;

		/* Software reset. */
		ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
				    LSM6DSM_CTRL3_ADDR, LSM6DSM_SW_RESET);
		if (ret != EC_SUCCESS)
			goto err_unlock;

		if (IS_ENABLED(CONFIG_LSM6DSM_SEC_I2C)) {
			/*
			 * Reboot to reload memory content as pass-through mode
			 * can get stuck.
			 * Direct to the AN: See "AN4987 - LSM6DSM: always-on 3D
			 * accelerometer and 3D gyroscope".
			 */

			/* Power ON Accel. */
			ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
					    ctrl_reg, reg_val);
			if (ret != EC_SUCCESS)
				goto err_unlock;

			ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
					    LSM6DSM_CTRL3_ADDR, LSM6DSM_BOOT);
			if (ret != EC_SUCCESS)
				goto err_unlock;

			/*
			 * Refer to AN4987, wait 15ms for accelerometer to doing
			 * full reboot.
			 */
			crec_msleep(15);

			/* Power OFF Accel. */
			ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
					    ctrl_reg, 0);
			if (ret != EC_SUCCESS)
				goto err_unlock;
		}

		/*
		 * Output data not updated until have been read.
		 * Prefer interrupt to be active low.
		 */
		ret = st_raw_write8(
			s->port, s->i2c_spi_addr_flags, LSM6DSM_CTRL3_ADDR,
			LSM6DSM_BDU | LSM6DSM_H_L_ACTIVE | LSM6DSM_IF_INC);
		if (ret != EC_SUCCESS)
			goto err_unlock;

		ret = fifo_disable(s);
		if (ret != EC_SUCCESS)
			goto err_unlock;

		if (IS_ENABLED(ACCEL_LSM6DSM_INT_ENABLE))
			ret = config_interrupt(s);
		if (ret != EC_SUCCESS)
			goto err_unlock;

		mutex_unlock(s->mutex);
	}

	/* Set default resolution common to acc and gyro. */
	data->resol = LSM6DSM_RESOLUTION;
	return sensor_init_done(s);

err_unlock:
	mutex_unlock(s->mutex);
	CPRINTS("%s: MS Init type:0x%X Error", s->name, s->type);
	return ret;
}

static int read_temp(const struct motion_sensor_t *s, int *temp)
{
	uint8_t raw[2];

	RETURN_ERROR(st_raw_read_n_noinc(s->port, s->i2c_spi_addr_flags,
					 LSM6DSM_OUT_TEMP_L_ADDR, raw, 2));
	*temp = C_TO_K(25 + (int)raw[1]);

	return EC_SUCCESS;
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

const struct accelgyro_drv lsm6dsm_drv = {
	.init = init,
	.read = read,
	.set_range = set_range,
	.get_resolution = st_get_resolution,
	.set_data_rate = lsm6dsm_set_data_rate,
	.get_data_rate = st_get_data_rate,
	.read_temp = read_temp,
	.set_offset = st_set_offset,
	.get_offset = st_get_offset,
#ifdef ACCEL_LSM6DSM_INT_ENABLE
	.irq_handler = irq_handler,
#endif /* ACCEL_LSM6DSM_INT_ENABLE */
#ifdef CONFIG_BODY_DETECTION
	.get_rms_noise = get_rms_noise,
#endif
#ifdef CONFIG_GESTURE_HOST_DETECTION
	.list_activities = st_list_activities,
#endif
};
