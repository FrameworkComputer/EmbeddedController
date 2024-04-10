/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Bosch Accelerometer driver for Chrome EC
 *
 * Supported: BMA255
 */

#include "accel_bma2x2.h"
#include "accelgyro.h"
#include "builtin/assert.h"
#include "common.h"
#include "console.h"
#include "i2c.h"
#include "math_util.h"
#include "spi.h"
#include "task.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ##args)

/* Number of times to attempt to enable sensor before giving up. */
#define SENSOR_ENABLE_ATTEMPTS 5

/**
 * Read register from accelerometer.
 */
static inline int raw_read8(const int port, const uint16_t i2c_addr_flags,
			    const int reg, int *data_ptr)
{
	return i2c_read8(port, i2c_addr_flags, reg, data_ptr);
}

/**
 * Write register from accelerometer.
 */
static inline int raw_write8(const int port, const uint16_t i2c_addr_flags,
			     const int reg, int data)
{
	return i2c_write8(port, i2c_addr_flags, reg, data);
}

static int set_range(struct motion_sensor_t *s, int range, int rnd)
{
	int ret, range_val, reg_val, range_reg_val;

	/* Range has to be between 2G-16G */
	if (range < 2)
		range = 2;
	else if (range > 16)
		range = 16;

	range_val = BMA2x2_RANGE_TO_REG(range);
	if ((BMA2x2_REG_TO_RANGE(range_val) < range) && rnd)
		range_val = BMA2x2_RANGE_TO_REG(range * 2);

	mutex_lock(s->mutex);

	/* Determine the new value of control reg and attempt to write it. */
	ret = raw_read8(s->port, s->i2c_spi_addr_flags,
			BMA2x2_RANGE_SELECT_ADDR, &range_reg_val);
	if (ret != EC_SUCCESS) {
		mutex_unlock(s->mutex);
		return ret;
	}
	reg_val = (range_reg_val & ~BMA2x2_RANGE_SELECT_MSK) | range_val;
	ret = raw_write8(s->port, s->i2c_spi_addr_flags,
			 BMA2x2_RANGE_SELECT_ADDR, reg_val);

	/* If successfully written, then save the range. */
	if (ret == EC_SUCCESS)
		s->current_range = BMA2x2_REG_TO_RANGE(range_val);

	mutex_unlock(s->mutex);

	return ret;
}

static int get_resolution(const struct motion_sensor_t *s)
{
	return BMA2x2_RESOLUTION;
}

static int set_data_rate(const struct motion_sensor_t *s, int rate, int rnd)
{
	int ret, odr_val, odr_reg_val, reg_val;
	struct accelgyro_saved_data_t *data = s->drv_data;

	/* Rate has to be between 7.8125Hz - 1000Hz */
	if (rate < 7813) {
		rate = 7812;
		odr_val = BMA2x2_BW_7_81HZ;
	} else if (rate > 1000000) {
		rate = 1000000;
		odr_val = BMA2x2_BW_1000HZ;
	} else {
		odr_val = BMA2x2_BW_TO_REG(rate);
		if ((BMA2x2_REG_TO_BW(odr_val) < rate) && rnd)
			odr_val = BMA2x2_BW_TO_REG(rate * 2);
	}

	mutex_lock(s->mutex);

	/* Determine the new value of control reg and attempt to write it. */
	ret = raw_read8(s->port, s->i2c_spi_addr_flags, BMA2x2_BW_SELECT_ADDR,
			&odr_reg_val);
	if (ret != EC_SUCCESS) {
		mutex_unlock(s->mutex);
		return ret;
	}
	reg_val = (odr_reg_val & ~BMA2x2_BW_MSK) | odr_val;
	/* Set output data rate. */
	ret = raw_write8(s->port, s->i2c_spi_addr_flags, BMA2x2_BW_SELECT_ADDR,
			 reg_val);

	/* If successfully written, then save the new data rate. */
	if (ret == EC_SUCCESS)
		data->odr = BMA2x2_REG_TO_BW(odr_val);

	mutex_unlock(s->mutex);
	return ret;
}

static int get_data_rate(const struct motion_sensor_t *s)
{
	struct accelgyro_saved_data_t *data = s->drv_data;

	return data->odr;
}

static int set_offset(const struct motion_sensor_t *s, const int16_t *offset,
		      int16_t temp)
{
	int i, ret;
	intv3_t v = { offset[X], offset[Y], offset[Z] };

	rotate_inv(v, *s->rot_standard_ref, v);

	/* temperature is ignored */
	/* Offset from host is in 1/1024g, 1/128g internally. */
	for (i = X; i <= Z; i++) {
		ret = raw_write8(s->port, s->i2c_spi_addr_flags,
				 BMA2x2_OFFSET_X_AXIS_ADDR + i, v[i] / 8);
		if (ret)
			return ret;
	}
	return EC_SUCCESS;
}

static int get_offset(const struct motion_sensor_t *s, int16_t *offset,
		      int16_t *temp)
{
	int i, val, ret;
	intv3_t v;

	for (i = X; i <= Z; i++) {
		ret = raw_read8(s->port, s->i2c_spi_addr_flags,
				BMA2x2_OFFSET_X_AXIS_ADDR + i, &val);
		if (ret)
			return ret;
		v[i] = (int8_t)val * 8;
	}
	rotate(v, *s->rot_standard_ref, v);
	offset[X] = v[X];
	offset[Y] = v[Y];
	offset[Z] = v[Z];

	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static int read(const struct motion_sensor_t *s, intv3_t v)
{
	uint8_t acc[6];
	int ret, i;

	/* Read 6 bytes starting at X_AXIS_LSB. */
	mutex_lock(s->mutex);
	ret = i2c_read_block(s->port, s->i2c_spi_addr_flags,
			     BMA2x2_X_AXIS_LSB_ADDR, acc, 6);
	mutex_unlock(s->mutex);

	if (ret != EC_SUCCESS)
		return ret;

	/*
	 * Convert acceleration to a signed 16-bit number. Note, based on
	 * the order of the registers:
	 *
	 * acc[0] = X_AXIS_LSB -> bit 7~4 for value, bit 0 for new data bit
	 * acc[1] = X_AXIS_MSB
	 * acc[2] = Y_AXIS_LSB -> bit 7~4 for value, bit 0 for new data bit
	 * acc[3] = Y_AXIS_MSB
	 * acc[4] = Z_AXIS_LSB -> bit 7~4 for value, bit 0 for new data bit
	 * acc[5] = Z_AXIS_MSB
	 */
	for (i = X; i <= Z; i++)
		v[i] = (((int8_t)acc[i * 2 + 1]) << 8) | (acc[i * 2] & 0xf0);
	rotate(v, *s->rot_standard_ref, v);

	return EC_SUCCESS;
}

static int perform_calib(struct motion_sensor_t *s, int enable)
{
	int ret, val, status, rate, range, i;
	timestamp_t deadline;

	if (!enable)
		return EC_SUCCESS;

	ret = raw_read8(s->port, s->i2c_spi_addr_flags, BMA2x2_OFFSET_CTRL_ADDR,
			&val);
	if (ret)
		return ret;
	if (!(val & BMA2x2_OFFSET_CAL_READY))
		return EC_ERROR_ACCESS_DENIED;

	rate = get_data_rate(s);
	range = s->current_range;
	/*
	 * Temporary set frequency to 100Hz to get enough data in a short
	 * period of time.
	 */
	set_data_rate(s, 100000, 0);
	set_range(s, 2, 0);

	/* We assume the device is laying flat for calibration */
	if (s->rot_standard_ref == NULL ||
	    (*s->rot_standard_ref)[2][2] > INT_TO_FP(0))
		val = BMA2x2_OFC_TARGET_PLUS_1G;
	else
		val = BMA2x2_OFC_TARGET_MINUS_1G;
	val = ((BMA2x2_OFC_TARGET_0G << BMA2x2_OFC_TARGET_AXIS(X)) |
	       (BMA2x2_OFC_TARGET_0G << BMA2x2_OFC_TARGET_AXIS(Y)) |
	       (val << BMA2x2_OFC_TARGET_AXIS(Z)));
	raw_write8(s->port, s->i2c_spi_addr_flags, BMA2x2_OFC_SETTING_ADDR,
		   val);

	for (i = X; i <= Z; i++) {
		val = (i + 1) << BMA2x2_OFFSET_TRIGGER_OFF;
		raw_write8(s->port, s->i2c_spi_addr_flags,
			   BMA2x2_OFFSET_CTRL_ADDR, val);
		/*
		 * The sensor needs 16 samples. At 100Hz/10ms, it needs 160ms to
		 * complete. Set 400ms to have some margin.
		 */
		deadline.val = get_time().val + 400 * MSEC;
		do {
			if (timestamp_expired(deadline, NULL)) {
				ret = EC_RES_TIMEOUT;
				goto end_perform_calib;
			}
			crec_msleep(50);
			ret = raw_read8(s->port, s->i2c_spi_addr_flags,
					BMA2x2_OFFSET_CTRL_ADDR, &status);
			if (ret != EC_SUCCESS)
				goto end_perform_calib;
		} while ((status & BMA2x2_OFFSET_CAL_READY) == 0);
	}

end_perform_calib:
	set_range(s, range, 0);
	set_data_rate(s, rate, 0);
	return ret;
}

static int init(struct motion_sensor_t *s)
{
	int ret = 0, tries = 0, val, reg, reset_field;

	/* This driver requires a mutex */
	ASSERT(s->mutex);

	ret = raw_read8(s->port, s->i2c_spi_addr_flags, BMA2x2_CHIP_ID_ADDR,
			&val);
	if (ret)
		return EC_ERROR_UNKNOWN;

	if (val != BMA255_CHIP_ID_MAJOR)
		return EC_ERROR_ACCESS_DENIED;

	/* Reset the chip to be in a good state */
	reg = BMA2x2_RST_ADDR;
	reset_field = BMA2x2_CMD_SOFT_RESET;

	mutex_lock(s->mutex);

	ret = raw_read8(s->port, s->i2c_spi_addr_flags, reg, &val);
	if (ret != EC_SUCCESS) {
		mutex_unlock(s->mutex);
		return ret;
	}
	val |= reset_field;
	ret = raw_write8(s->port, s->i2c_spi_addr_flags, reg, val);
	if (ret != EC_SUCCESS) {
		mutex_unlock(s->mutex);
		return ret;
	}

	/* The SRST will be cleared when reset is complete. */
	do {
		ret = raw_read8(s->port, s->i2c_spi_addr_flags, reg, &val);

		/* Reset complete. */
		if ((ret == EC_SUCCESS) && !(val & reset_field))
			break;

		/* Check for tires. */
		if (tries++ > SENSOR_ENABLE_ATTEMPTS) {
			ret = EC_ERROR_TIMEOUT;
			mutex_unlock(s->mutex);
			return ret;
		}
		crec_msleep(1);
	} while (1);
	mutex_unlock(s->mutex);

	return sensor_init_done(s);
}

const struct accelgyro_drv bma2x2_accel_drv = {
	.init = init,
	.read = read,
	.set_range = set_range,
	.get_resolution = get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = get_data_rate,
	.set_offset = set_offset,
	.get_offset = get_offset,
	.perform_calib = perform_calib,
};
