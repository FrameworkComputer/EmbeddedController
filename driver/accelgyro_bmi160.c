/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * BMI160 accelerometer and gyro module for Chrome EC
 * 3D digital accelerometer & 3D digital gyroscope
 */

#include "accelgyro.h"
#include "accelgyro_bmi160.h"
#include "accelgyro_bmi_common.h"
#include "builtin/assert.h"
#include "common.h"
#include "console.h"
#include "hwtimer.h"
#include "i2c.h"
#include "mag_bmm150.h"
#include "math_util.h"
#include "motion_orientation.h"
#include "motion_sense_fifo.h"
#include "spi.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ##args)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ##args)

#ifdef CONFIG_ACCELGYRO_BMI160_INT_EVENT
#define ACCELGYRO_BMI160_INT_ENABLE
#endif

STATIC_IF(CONFIG_BMI_ORIENTATION_SENSOR)
void irq_set_orientation(struct motion_sensor_t *s, int interrupt);

STATIC_IF(ACCELGYRO_BMI160_INT_ENABLE)
volatile uint32_t last_interrupt_timestamp;

static int wakeup_time[] = { [MOTIONSENSE_TYPE_ACCEL] = 4,
			     [MOTIONSENSE_TYPE_GYRO] = 80,
			     [MOTIONSENSE_TYPE_MAG] = 1 };

/**
 * Control access to the compass on the secondary i2c interface:
 * enable values are:
 * 1: manual access, we can issue i2c to the compass
 * 0: data access: BMI160 gather data periodically from the compass.
 */
static __maybe_unused int
bmi160_sec_access_ctrl(const int port, const uint16_t i2c_spi_addr_flags,
		       const int enable)
{
	int mag_if_ctrl;
	bmi_read8(port, i2c_spi_addr_flags, BMI160_MAG_IF_1, &mag_if_ctrl);
	if (enable) {
		mag_if_ctrl |= BMI160_MAG_MANUAL_EN;
		mag_if_ctrl &= ~BMI160_MAG_READ_BURST_MASK;
		mag_if_ctrl |= BMI160_MAG_READ_BURST_1;
	} else {
		mag_if_ctrl &= ~BMI160_MAG_MANUAL_EN;
		mag_if_ctrl &= ~BMI160_MAG_READ_BURST_MASK;
		mag_if_ctrl |= BMI160_MAG_READ_BURST_8;
	}
	return bmi_write8(port, i2c_spi_addr_flags, BMI160_MAG_IF_1,
			  mag_if_ctrl);
}

/**
 * Read register from compass.
 * Assuming we are in manual access mode, read compass i2c register.
 */
int bmi160_sec_raw_read8(const int port, const uint16_t i2c_spi_addr_flags,
			 const uint8_t reg, int *data_ptr)
{
	/* Only read 1 bytes */
	bmi_write8(port, i2c_spi_addr_flags, BMI160_MAG_I2C_READ_ADDR, reg);
	return bmi_read8(port, i2c_spi_addr_flags, BMI160_MAG_I2C_READ_DATA,
			 data_ptr);
}

/**
 * Write register from compass.
 * Assuming we are in manual access mode, write to compass i2c register.
 */
int bmi160_sec_raw_write8(const int port, const uint16_t i2c_spi_addr_flags,
			  const uint8_t reg, int data)
{
	bmi_write8(port, i2c_spi_addr_flags, BMI160_MAG_I2C_WRITE_DATA, data);
	return bmi_write8(port, i2c_spi_addr_flags, BMI160_MAG_I2C_WRITE_ADDR,
			  reg);
}

static int set_data_rate(const struct motion_sensor_t *s, int rate, int rnd)
{
	int ret, normalized_rate;
	uint8_t reg_val;
	struct accelgyro_saved_data_t *data = BMI_GET_SAVED_DATA(s);

	if (rate == 0) {
		/* FIFO stop collecting events */
		if (IS_ENABLED(ACCELGYRO_BMI160_INT_ENABLE))
			bmi_enable_fifo(s, 0);

		/* go to suspend mode */
		ret = bmi_write8(s->port, s->i2c_spi_addr_flags, BMI160_CMD_REG,
				 BMI160_CMD_MODE_SUSPEND(s->type));
		msleep(3);
		data->odr = 0;
		if (IS_ENABLED(CONFIG_MAG_BMI_BMM150) &&
		    (s->type == MOTIONSENSE_TYPE_MAG)) {
			struct mag_cal_t *moc = BMM150_CAL(s);

			moc->batch_size = 0;
		}

		return ret;
	} else if (data->odr == 0) {
		/* back from suspend mode. */
		ret = bmi_write8(s->port, s->i2c_spi_addr_flags, BMI160_CMD_REG,
				 BMI160_CMD_MODE_NORMAL(s->type));
		msleep(wakeup_time[s->type]);
	}

	ret = bmi_get_normalized_rate(s, rate, rnd, &normalized_rate, &reg_val);
	if (ret)
		return ret;

	/*
	 * Lock accel resource to prevent another task from attempting
	 * to write accel parameters until we are done.
	 */
	mutex_lock(s->mutex);

	ret = bmi_set_reg8(s, BMI_CONF_REG(s->type), reg_val, BMI_ODR_MASK);
	if (ret != EC_SUCCESS)
		goto accel_cleanup;

	/* Now that we have set the odr, update the driver's value. */
	data->odr = normalized_rate;

	if (IS_ENABLED(CONFIG_MAG_BMI_BMM150) &&
	    (s->type == MOTIONSENSE_TYPE_MAG)) {
		struct mag_cal_t *moc = BMM150_CAL(s);

		/* Reset the calibration */
		init_mag_cal(moc);
		/*
		 * We need at least MIN_BATCH_SIZE amd we must have collected
		 * for at least MIN_BATCH_WINDOW_US.
		 * Given odr is in mHz, multiply by 1000x
		 */
		moc->batch_size =
			MAX(MAG_CAL_MIN_BATCH_SIZE,
			    (data->odr * 1000) / (MAG_CAL_MIN_BATCH_WINDOW_US));
		CPRINTS("Batch size: %d", moc->batch_size);
	}

	/*
	 * FIFO start collecting events.
	 * They will be discarded if AP does not want them.
	 */
	if (IS_ENABLED(ACCELGYRO_BMI160_INT_ENABLE))
		bmi_enable_fifo(s, 1);

accel_cleanup:
	mutex_unlock(s->mutex);
	return ret;
}

static int set_offset(const struct motion_sensor_t *s, const int16_t *offset,
		      int16_t temp)
{
	int ret, val98;
	intv3_t v = { offset[X], offset[Y], offset[Z] };

	rotate_inv(v, *s->rot_standard_ref, v);

	ret = bmi_read8(s->port, s->i2c_spi_addr_flags, BMI160_OFFSET_EN_GYR98,
			&val98);
	if (ret != 0)
		return ret;

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		ret = bmi_set_accel_offset(s, v);
		if (ret != EC_SUCCESS)
			return ret;

		ret = bmi_write8(s->port, s->i2c_spi_addr_flags,
				 BMI160_OFFSET_EN_GYR98,
				 val98 | BMI160_OFFSET_ACC_EN);
		break;
	case MOTIONSENSE_TYPE_GYRO:
		ret = bmi_set_gyro_offset(s, v, &val98);
		if (ret != EC_SUCCESS)
			return ret;

		ret = bmi_write8(s->port, s->i2c_spi_addr_flags,
				 BMI160_OFFSET_EN_GYR98,
				 val98 | BMI160_OFFSET_GYRO_EN);
		break;
#ifdef CONFIG_MAG_BMI_BMM150
	case MOTIONSENSE_TYPE_MAG:
		ret = bmm150_set_offset(s, v);
		break;
#endif /* defined(CONFIG_MAG_BMI_BMM150) */
	default:
		ret = EC_RES_INVALID_PARAM;
	}
	return ret;
}

static int perform_calib(struct motion_sensor_t *s, int enable)
{
	int ret, val, en_flag, status, rate, range = s->current_range;
	timestamp_t deadline, timeout;

	if (!enable)
		return EC_SUCCESS;

	/* We only support accelerometers and gyroscopes */
	if (s->type != MOTIONSENSE_TYPE_ACCEL &&
	    s->type != MOTIONSENSE_TYPE_GYRO)
		return EC_RES_INVALID_PARAM;

	rate = bmi_get_data_rate(s);
	/*
	 * Temporary set frequency to 100Hz to get enough data in a short
	 * period of time.
	 */
	ret = set_data_rate(s, 100000, 0);
	if (ret != EC_SUCCESS)
		goto end_perform_calib;

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		/* We assume the device is laying flat for calibration */
		if (s->rot_standard_ref == NULL ||
		    (*s->rot_standard_ref)[2][2] > INT_TO_FP(0))
			val = BMI160_FOC_ACC_PLUS_1G;
		else
			val = BMI160_FOC_ACC_MINUS_1G;
		val = (BMI160_FOC_ACC_0G << BMI160_FOC_ACC_X_OFFSET) |
		      (BMI160_FOC_ACC_0G << BMI160_FOC_ACC_Y_OFFSET) |
		      (val << BMI160_FOC_ACC_Z_OFFSET);
		en_flag = BMI160_OFFSET_ACC_EN;
		/*
		 * Temporary set range to minimum to run calibration with
		 * full sensitivity
		 */
		bmi_set_range(s, 2, 0);
		/* Timeout for accelerometer calibration */
		timeout.val = 400 * MSEC;
		break;
	case MOTIONSENSE_TYPE_GYRO:
		val = BMI160_FOC_GYRO_EN;
		en_flag = BMI160_OFFSET_GYRO_EN;
		/*
		 * Temporary set range to minimum to run calibration with
		 * full sensitivity
		 */
		bmi_set_range(s, 125, 0);
		/* Timeout for gyroscope calibration */
		timeout.val = 800 * MSEC;
		break;
	/* LCOV_EXCL_START */
	default:
		/* Unreachable due to sensor type check above. */
		ASSERT(false);
		return EC_RES_INVALID_PARAM;
		/* LCOV_EXCL_STOP */
	}
	ret = bmi_write8(s->port, s->i2c_spi_addr_flags, BMI160_FOC_CONF, val);
	ret = bmi_write8(s->port, s->i2c_spi_addr_flags, BMI160_CMD_REG,
			 BMI160_CMD_START_FOC);
	deadline.val = get_time().val + timeout.val;
	do {
		if (timestamp_expired(deadline, NULL)) {
			ret = EC_RES_TIMEOUT;
			goto end_perform_calib;
		}
		msleep(50);
		ret = bmi_read8(s->port, s->i2c_spi_addr_flags, BMI160_STATUS,
				&status);
		if (ret != EC_SUCCESS)
			goto end_perform_calib;
	} while ((status & BMI160_FOC_RDY) == 0);

	/* Calibration is successful, and loaded, use the result */
	ret = bmi_enable_reg8(s, BMI160_OFFSET_EN_GYR98, en_flag, 1);
end_perform_calib:
	bmi_set_range(s, range, 0);
	set_data_rate(s, rate, 0);
	return ret;
}

/*
 * Manage gesture recognition.
 * Defined even if host interface is not defined, to enable double tap even
 * when the host does not deal with gesture.
 */
#ifdef CONFIG_GESTURE_HOST_DETECTION
static int manage_activity(const struct motion_sensor_t *s,
			   enum motionsensor_activity activity, int enable,
			   const struct ec_motion_sense_activity *param)
{
	int ret;
	struct bmi_drv_data_t *data = BMI_GET_DATA(s);

	switch (activity) {
#ifdef CONFIG_GESTURE_SIGMO
	case MOTIONSENSE_ACTIVITY_SIG_MOTION: {
		if (enable) {
			/* We should use parameters from caller */
			bmi_write8(s->port, s->i2c_spi_addr_flags,
				   BMI160_INT_MOTION_3,
				   BMI160_MOTION_PROOF_TIME(
					   CONFIG_GESTURE_SIGMO_PROOF_MS)
						   << BMI160_MOTION_PROOF_OFF |
					   BMI160_MOTION_SKIP_TIME(
						   CONFIG_GESTURE_SIGMO_SKIP_MS)
						   << BMI160_MOTION_SKIP_OFF |
					   BMI160_MOTION_SIG_MOT_SEL);
			bmi_write8(s->port, s->i2c_spi_addr_flags,
				   BMI160_INT_MOTION_1,
				   BMI160_MOTION_TH(
					   s, CONFIG_GESTURE_SIGMO_THRES_MG));
		}
		ret = bmi_enable_reg8(s, BMI160_INT_EN_0,
				      BMI160_INT_ANYMO_X_EN |
					      BMI160_INT_ANYMO_Y_EN |
					      BMI160_INT_ANYMO_Z_EN,
				      enable);
		if (ret)
			ret = EC_RES_UNAVAILABLE;
		break;
	}
#endif
#ifdef CONFIG_GESTURE_SENSOR_DOUBLE_TAP
	case MOTIONSENSE_ACTIVITY_DOUBLE_TAP: {
		/* Set double tap interrupt */
		ret = bmi_enable_reg8(s, BMI160_INT_EN_0, BMI160_INT_D_TAP_EN,
				      enable);
		if (ret)
			ret = EC_RES_UNAVAILABLE;
		break;
	}
#endif
	default:
		ret = EC_RES_INVALID_PARAM;
	}
	if (ret == EC_RES_SUCCESS) {
		if (enable) {
			data->enabled_activities |= 1 << activity;
			data->disabled_activities &= ~BIT(activity);
		} else {
			data->enabled_activities &= ~BIT(activity);
			data->disabled_activities |= 1 << activity;
		}
	}
	return ret;
}
#endif

#ifdef CONFIG_BODY_DETECTION
static int get_rms_noise(const struct motion_sensor_t *s)
{
	return bmi_get_rms_noise(s, BMI160_ACCEL_RMS_NOISE_100HZ);
}
#endif

/** Requires that the passed sensor `*s` is an accelerometer */
static __maybe_unused int
config_accel_interrupt(const struct motion_sensor_t *s)
{
	int ret, tmp;

	mutex_lock(s->mutex);
	bmi_write8(s->port, s->i2c_spi_addr_flags, BMI160_CMD_REG,
		   BMI160_CMD_FIFO_FLUSH);
	bmi_write8(s->port, s->i2c_spi_addr_flags, BMI160_CMD_REG,
		   BMI160_CMD_INT_RESET);

	if (IS_ENABLED(CONFIG_GESTURE_SENSOR_DOUBLE_TAP)) {
		bmi_write8(s->port, s->i2c_spi_addr_flags, BMI160_INT_TAP_0,
			   BMI160_TAP_DUR(s,
					  CONFIG_GESTURE_TAP_MAX_INTERSTICE_T));
		ret = bmi_write8(s->port, s->i2c_spi_addr_flags,
				 BMI160_INT_TAP_1,
				 BMI160_TAP_TH(s, CONFIG_GESTURE_TAP_THRES_MG));
	}
	/* only use orientation sensor on the lid sensor */
	if (IS_ENABLED(CONFIG_BMI_ORIENTATION_SENSOR) &&
	    (s->location == MOTIONSENSE_LOC_LID)) {
		ret = bmi_write8(s->port, s->i2c_spi_addr_flags,
				 BMI160_INT_ORIENT_0,
				 BMI160_INT_ORIENT_0_INIT_VAL);
		ret = bmi_write8(s->port, s->i2c_spi_addr_flags,
				 BMI160_INT_ORIENT_1,
				 BMI160_INT_ORIENT_1_INIT_VAL);
	}

	if (IS_ENABLED(CONFIG_ACCELGYRO_BMI160_INT2_OUTPUT)) {
		ret = bmi_write8(s->port, s->i2c_spi_addr_flags,
				 BMI160_INT_LATCH, BMI160_LATCH_5MS);
	} else {
		/* Also, configure int2 as an external input. */
		ret = bmi_write8(s->port, s->i2c_spi_addr_flags,
				 BMI160_INT_LATCH,
				 BMI160_INT2_INPUT_EN | BMI160_LATCH_5MS);
	}

	/* configure int1 as an interrupt */
	ret = bmi_write8(s->port, s->i2c_spi_addr_flags, BMI160_INT_OUT_CTRL,
			 BMI160_INT_CTRL(1, OUTPUT_EN));

	/* Map activity interrupt to int 1 */
	tmp = 0;
	if (IS_ENABLED(CONFIG_GESTURE_SIGMO)) {
		tmp |= BMI160_INT_ANYMOTION;
	} else if (IS_ENABLED(CONFIG_GESTURE_SENSOR_DOUBLE_TAP)) {
		tmp |= BMI160_INT_D_TAP;
	} else if (IS_ENABLED(CONFIG_BMI_ORIENTATION_SENSOR) &&
		   (s->location == MOTIONSENSE_LOC_LID)) {
		/* enable orientation interrupt for lid sensor only */
		tmp |= BMI160_INT_ORIENT;
	}
	ret = bmi_write8(s->port, s->i2c_spi_addr_flags, BMI160_INT_MAP_REG(1),
			 tmp);

	if (IS_ENABLED(ACCELGYRO_BMI160_INT_ENABLE)) {
		/* map fifo water mark to int 1 */
		ret = bmi_write8(
			s->port, s->i2c_spi_addr_flags, BMI160_INT_FIFO_MAP,
			BMI160_INT_MAP(1, FWM) | BMI160_INT_MAP(1, FFULL));

		/*
		 * Configure fifo watermark to int whenever there's any data in
		 * there
		 */
		ret = bmi_write8(s->port, s->i2c_spi_addr_flags,
				 BMI160_FIFO_CONFIG_0, 1);
		if (IS_ENABLED(CONFIG_ACCELGYRO_BMI160_INT2_OUTPUT))
			ret = bmi_write8(s->port, s->i2c_spi_addr_flags,
					 BMI160_FIFO_CONFIG_1,
					 BMI160_FIFO_HEADER_EN);
		else
			ret = bmi_write8(s->port, s->i2c_spi_addr_flags,
					 BMI160_FIFO_CONFIG_1,
					 BMI160_FIFO_TAG_INT2_EN |
						 BMI160_FIFO_HEADER_EN);

		/* Set fifo*/
		bmi_enable_reg8(s, BMI160_INT_EN_1,
				BMI160_INT_FWM_EN | BMI160_INT_FFUL_EN, 1);
	}
	mutex_unlock(s->mutex);
	return ret;
}

#ifdef ACCELGYRO_BMI160_INT_ENABLE
#ifdef CONFIG_BMI_ORIENTATION_SENSOR
static void irq_set_orientation(struct motion_sensor_t *s, int interrupt)
{
	int shifted_masked_orientation = (interrupt >> 24) &
					 BMI160_ORIENT_XY_MASK;
	if (BMI_GET_DATA(s)->raw_orientation != shifted_masked_orientation) {
		enum motionsensor_orientation orientation =
			MOTIONSENSE_ORIENTATION_UNKNOWN;

		BMI_GET_DATA(s)->raw_orientation = shifted_masked_orientation;

		switch (shifted_masked_orientation) {
		case BMI160_ORIENT_PORTRAIT:
			orientation = MOTIONSENSE_ORIENTATION_PORTRAIT;
			break;
		case BMI160_ORIENT_PORTRAIT_INVERT:
			orientation =
				MOTIONSENSE_ORIENTATION_UPSIDE_DOWN_PORTRAIT;
			break;
		case BMI160_ORIENT_LANDSCAPE:
			orientation = MOTIONSENSE_ORIENTATION_LANDSCAPE;
			break;
		case BMI160_ORIENT_LANDSCAPE_INVERT:
			orientation =
				MOTIONSENSE_ORIENTATION_UPSIDE_DOWN_LANDSCAPE;
			break;
		default:
			break;
		}
		orientation = motion_orientation_remap(s, orientation);
		*motion_orientation_ptr(s) = orientation;
	}
}
#endif /* CONFIG_BMI_ORIENTATION_SENSOR */

/**
 * bmi160_interrupt - called when the sensor activates the interrupt line.
 *
 * This is a "top half" interrupt handler, it just asks motion sense ask
 * to schedule the "bottom half", ->irq_handler().
 */
void bmi160_interrupt(enum gpio_signal signal)
{
	last_interrupt_timestamp = __hw_clock_source_read();

	task_set_event(TASK_ID_MOTIONSENSE, CONFIG_ACCELGYRO_BMI160_INT_EVENT);
}

/**
 * irq_handler - bottom half of the interrupt stack.
 * Ran from the motion_sense task, finds the events that raised the interrupt.
 *
 * For now, we just print out. We should set a bitmask motion sense code will
 * act upon.
 */
static int irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	uint32_t interrupt;
	int8_t has_read_fifo = 0;
	int rv;
	int i;

	if ((s->type != MOTIONSENSE_TYPE_ACCEL) ||
	    (!(*event & CONFIG_ACCELGYRO_BMI160_INT_EVENT)))
		return EC_ERROR_NOT_HANDLED;

	/*
	 * We have to loop until we see the interrupt status as 0 to avoid
	 * getting stuck. We use edge triggered interrupts and, once one
	 * triggers, our irq apparently won't necessarily trigger again until
	 * we've cleared all interrupt sources and then a new interrupt happens.
	 *
	 * However, despite needing to loop, we also don't want to get stuck
	 * in an infinite loop if there's a bug in the driver or the hardware.
	 * We'll loop 200 times and then give up if an interrupt is still
	 * pending.
	 */
	for (i = 0; i < 200; i++) {
		rv = bmi_read16(s->port, s->i2c_spi_addr_flags,
				BMI160_INT_STATUS_0, &interrupt);

		/* Bail out if there was an error or no more interrupts. */
		if (rv || !interrupt)
			break;

		if (IS_ENABLED(CONFIG_GESTURE_SENSOR_DOUBLE_TAP) &&
		    (interrupt & BMI160_D_TAP_INT))
			*event |= TASK_EVENT_MOTION_ACTIVITY_INTERRUPT(
				MOTIONSENSE_ACTIVITY_DOUBLE_TAP);
		if (IS_ENABLED(CONFIG_GESTURE_SIGMO) &&
		    (interrupt & BMI160_SIGMOT_INT))
			*event |= TASK_EVENT_MOTION_ACTIVITY_INTERRUPT(
				MOTIONSENSE_ACTIVITY_SIG_MOTION);
		if (interrupt & (BMI160_FWM_INT | BMI160_FFULL_INT)) {
			bmi_load_fifo(s, last_interrupt_timestamp);
			has_read_fifo = 1;
		}
		if (IS_ENABLED(CONFIG_BMI_ORIENTATION_SENSOR))
			irq_set_orientation(s, interrupt);
	}

	if (i == 200) {
		CPRINTF("BMI160 irq 0x%04x stuck (%d loops)\n", interrupt, i);
		bmi_write8(s->port, s->i2c_spi_addr_flags, BMI160_CMD_REG,
			   BMI160_CMD_FIFO_FLUSH);
	}

	/* Only return an error if no data was read at all. */
	if (i == 0 && rv)
		return rv;

	if (IS_ENABLED(CONFIG_ACCEL_FIFO) && has_read_fifo)
		motion_sense_fifo_commit_data();

	return EC_SUCCESS;
}
#endif /* ACCELGYRO_BMI160_INT_ENABLE */

static int init(struct motion_sensor_t *s)
{
	int ret = 0, tmp, i;
	struct accelgyro_saved_data_t *saved_data = BMI_GET_SAVED_DATA(s);

	ret = bmi_read8(s->port, s->i2c_spi_addr_flags, BMI160_CHIP_ID, &tmp);
	if (ret)
		return EC_ERROR_UNKNOWN;

	if (tmp != BMI160_CHIP_ID_MAJOR && tmp != BMI168_CHIP_ID_MAJOR) {
		/* The device may be lock on paging mode. Try to unlock it. */
		bmi_write8(s->port, s->i2c_spi_addr_flags, BMI160_CMD_REG,
			   BMI160_CMD_EXT_MODE_EN_B0);
		bmi_write8(s->port, s->i2c_spi_addr_flags, BMI160_CMD_REG,
			   BMI160_CMD_EXT_MODE_EN_B1);
		bmi_write8(s->port, s->i2c_spi_addr_flags, BMI160_CMD_REG,
			   BMI160_CMD_EXT_MODE_EN_B2);
		bmi_write8(s->port, s->i2c_spi_addr_flags,
			   BMI160_CMD_EXT_MODE_ADDR, BMI160_CMD_PAGING_EN);
		bmi_write8(s->port, s->i2c_spi_addr_flags,
			   BMI160_CMD_EXT_MODE_ADDR, 0);
		return EC_ERROR_ACCESS_DENIED;
	}

	if (s->type == MOTIONSENSE_TYPE_ACCEL) {
		struct bmi_drv_data_t *data = BMI_GET_DATA(s);

		/* Reset the chip to be in a good state */
		bmi_write8(s->port, s->i2c_spi_addr_flags, BMI160_CMD_REG,
			   BMI160_CMD_SOFT_RESET);
		msleep(1);
		data->flags &= ~(BMI_FLAG_SEC_I2C_ENABLED |
				 (BMI_FIFO_ALL_MASK << BMI_FIFO_FLAG_OFFSET));
		if (IS_ENABLED(CONFIG_GESTURE_HOST_DETECTION)) {
			data->enabled_activities = 0;
			data->disabled_activities = 0;
			if (IS_ENABLED(CONFIG_GESTURE_SIGMO))
				data->disabled_activities |=
					BIT(MOTIONSENSE_ACTIVITY_SIG_MOTION);
			if (IS_ENABLED(CONFIG_GESTURE_SENSOR_DOUBLE_TAP))
				data->disabled_activities |=
					BIT(MOTIONSENSE_ACTIVITY_DOUBLE_TAP);
		}
		/* To avoid gyro wakeup */
		bmi_write8(s->port, s->i2c_spi_addr_flags, BMI160_PMU_TRIGGER,
			   0);
	}

#ifdef CONFIG_BMI_SEC_I2C
	if (s->type == MOTIONSENSE_TYPE_MAG) {
		struct bmi_drv_data_t *data = BMI_GET_DATA(s);

		/*
		 * To be able to configure the real magnetometer, we must set
		 * the BMI160 magnetometer part (a pass through) in normal mode.
		 */
		bmi_write8(s->port, s->i2c_spi_addr_flags, BMI160_CMD_REG,
			   BMI160_CMD_MODE_NORMAL(s->type));
		msleep(wakeup_time[s->type]);

		if ((data->flags & BMI_FLAG_SEC_I2C_ENABLED) == 0) {
			int ext_page_reg;
			/* Enable secondary interface */
			/*
			 * This is not part of the normal configuration but from
			 * code on Bosh github repo:
			 * https://github.com/BoschSensortec/BMI160_driver
			 *
			 * Magic command sequences
			 */
			bmi_write8(s->port, s->i2c_spi_addr_flags,
				   BMI160_CMD_REG, BMI160_CMD_EXT_MODE_EN_B0);
			bmi_write8(s->port, s->i2c_spi_addr_flags,
				   BMI160_CMD_REG, BMI160_CMD_EXT_MODE_EN_B1);
			bmi_write8(s->port, s->i2c_spi_addr_flags,
				   BMI160_CMD_REG, BMI160_CMD_EXT_MODE_EN_B2);

			/*
			 * Change the register page to target mode, to change
			 * the internal pull ups of the secondary interface.
			 */
			bmi_enable_reg8(s, BMI160_CMD_EXT_MODE_ADDR,
					BMI160_CMD_TARGET_PAGE, 1);
			bmi_enable_reg8(s, BMI160_CMD_EXT_MODE_ADDR,
					BMI160_CMD_PAGING_EN, 1);
			bmi_enable_reg8(s, BMI160_COM_C_TRIM_ADDR,
					BMI160_COM_C_TRIM, 1);
			bmi_enable_reg8(s, BMI160_CMD_EXT_MODE_ADDR,
					BMI160_CMD_TARGET_PAGE, 0);
			bmi_read8(s->port, s->i2c_spi_addr_flags,
				  BMI160_CMD_EXT_MODE_ADDR, &ext_page_reg);

			/* Set the i2c address of the compass */
			ret = bmi_write8(
				s->port, s->i2c_spi_addr_flags, BMI160_MAG_IF_0,
				I2C_STRIP_FLAGS(CONFIG_ACCELGYRO_SEC_ADDR_FLAGS)
					<< 1);

			/* Enable the secondary interface as I2C */
			ret = bmi_write8(
				s->port, s->i2c_spi_addr_flags, BMI160_IF_CONF,
				BMI160_IF_MODE_AUTO_I2C << BMI160_IF_MODE_OFF);
			data->flags |= BMI_FLAG_SEC_I2C_ENABLED;
		}

		bmi160_sec_access_ctrl(s->port, s->i2c_spi_addr_flags, 1);

		ret = bmm150_init(s);
		if (ret)
			/* Leave the compass open for tinkering. */
			return ret;

		/* Leave the address for reading the data */
		bmi_write8(s->port, s->i2c_spi_addr_flags,
			   BMI160_MAG_I2C_READ_ADDR, BMM150_BASE_DATA);
		/*
		 * Put back the secondary interface in normal mode.
		 * BMI160 will poll based on the configure ODR.
		 */
		bmi160_sec_access_ctrl(s->port, s->i2c_spi_addr_flags, 0);

		/*
		 * Clean interrupt event that may have occurred while the
		 * BMI160 was in management mode.
		 */
		task_set_event(TASK_ID_MOTIONSENSE,
			       CONFIG_ACCELGYRO_BMI160_INT_EVENT);
	}
#endif

	for (i = X; i <= Z; i++)
		saved_data->scale[i] = MOTION_SENSE_DEFAULT_SCALE;
	/*
	 * The sensor is in Suspend mode at init,
	 * so set data rate to 0.
	 */
	saved_data->odr = 0;

	if (IS_ENABLED(ACCELGYRO_BMI160_INT_ENABLE) &&
	    (s->type == MOTIONSENSE_TYPE_ACCEL))
		ret = config_accel_interrupt(s);

	return sensor_init_done(s);
}

const struct accelgyro_drv bmi160_drv = {
	.init = init,
	.read = bmi_read,
	.set_range = bmi_set_range,
	.get_resolution = bmi_get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = bmi_get_data_rate,
	.set_offset = set_offset,
	.get_scale = bmi_get_scale,
	.set_scale = bmi_set_scale,
	.get_offset = bmi_get_offset,
	.perform_calib = perform_calib,
	.read_temp = bmi_read_temp,
#ifdef ACCELGYRO_BMI160_INT_ENABLE
	.irq_handler = irq_handler,
#endif
#ifdef CONFIG_GESTURE_HOST_DETECTION
	.manage_activity = manage_activity,
	.list_activities = bmi_list_activities,
#endif
#ifdef CONFIG_BODY_DETECTION
	.get_rms_noise = get_rms_noise,
#endif
};

#ifdef CONFIG_CMD_I2C_STRESS_TEST_ACCEL
struct i2c_stress_test_dev bmi160_i2c_stress_test_dev = {
	.reg_info = {
		.read_reg = BMI160_CHIP_ID,
		.read_val = BMI160_CHIP_ID_MAJOR,
		.write_reg = BMI160_PMU_TRIGGER,
	},
	.i2c_read = &bmi_read8,
	.i2c_write = &bmi_write8,
};
#endif /* CONFIG_CMD_I2C_STRESS_TEST_ACCEL */

/*
 * TODO(chingkang): Replace bmi160_get_sensor_temp in some board config to
 *                  bmi_get_sensor_temp. Then, remove this definition.
 */
int bmi160_get_sensor_temp(int idx, int *temp_ptr)
{
	return bmi_get_sensor_temp(idx, temp_ptr);
}
