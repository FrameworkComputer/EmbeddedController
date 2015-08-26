/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* KXCJ9 gsensor module for Chrome EC */

#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "driver/accel_kxcj9.h"
#include "gpio.h"
#include "i2c.h"
#include "math_util.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ## args)

/* Number of times to attempt to enable sensor before giving up. */
#define SENSOR_ENABLE_ATTEMPTS 3

/*
 * Struct for pairing an engineering value with the register value for a
 * parameter.
 */
struct accel_param_pair {
	int val; /* Value in engineering units. */
	int reg; /* Corresponding register value. */
};

/* List of range values in +/-G's and their associated register values. */
static const struct accel_param_pair ranges[] = {
	{2, KXCJ9_GSEL_2G},
	{4, KXCJ9_GSEL_4G},
	{8, KXCJ9_GSEL_8G_14BIT}
};

/* List of resolution values in bits and their associated register values. */
static const struct accel_param_pair resolutions[] = {
	{8,  KXCJ9_RES_8BIT},
	{12, KXCJ9_RES_12BIT}
};

/* List of ODR values in mHz and their associated register values. */
static const struct accel_param_pair datarates[] = {
	{0,       KXCJ9_OSA_0_000HZ},
	{781,     KXCJ9_OSA_0_781HZ},
	{1563,    KXCJ9_OSA_1_563HZ},
	{3125,    KXCJ9_OSA_3_125HZ},
	{6250,    KXCJ9_OSA_6_250HZ},
	{12500,   KXCJ9_OSA_12_50HZ},
	{25000,   KXCJ9_OSA_25_00HZ},
	{50000,   KXCJ9_OSA_50_00HZ},
	{100000,  KXCJ9_OSA_100_0HZ},
	{200000,  KXCJ9_OSA_200_0HZ},
	{400000,  KXCJ9_OSA_400_0HZ},
	{800000,  KXCJ9_OSA_800_0HZ},
	{1600000, KXCJ9_OSA_1600_HZ}
};

/**
 * Find index into a accel_param_pair that matches the given engineering value
 * passed in. The round_up flag is used to specify whether to round up or down.
 * Note, this function always returns a valid index. If the request is
 * outside the range of values, it returns the closest valid index.
 */
static int find_param_index(const int eng_val, const int round_up,
		const struct accel_param_pair *pairs, const int size)
{
	int i;

	/* Linear search for index to match. */
	for (i = 0; i < size - 1; i++) {
		if (eng_val <= pairs[i].val)
			return i;

		if (eng_val < pairs[i+1].val) {
			if (round_up)
				return i + 1;
			else
				return i;
		}
	}

	return i;
}

/**
 * Read register from accelerometer.
 */
static int raw_read8(const int addr, const int reg, int *data_ptr)
{
	return i2c_read8(I2C_PORT_ACCEL, addr, reg, data_ptr);
}

/**
 * Write register from accelerometer.
 */
static int raw_write8(const int addr, const int reg, int data)
{
	return i2c_write8(I2C_PORT_ACCEL, addr, reg, data);
}

/**
 * Disable sensor by taking it out of operating mode. When disabled, the
 * acceleration data does not change.
 *
 * Note: This is intended to be called in a pair with enable_sensor().
 *
 * @data Pointer to motion sensor data
 * @ctrl1 Pointer to location to store KXCJ9_CTRL1 register after disabling
 *
 * @return EC_SUCCESS if successful, EC_ERROR_* otherwise
 */
static int disable_sensor(const struct motion_sensor_t *s, int *ctrl1)
{
	int i, ret;

	/*
	 * Read the current state of the ctrl1 register
	 * so that we can restore it later.
	 */
	for (i = 0; i < SENSOR_ENABLE_ATTEMPTS; i++) {
		ret = raw_read8(s->addr, KXCJ9_CTRL1, ctrl1);
		if (ret != EC_SUCCESS)
			continue;

		*ctrl1 &= ~KXCJ9_CTRL1_PC1;

		ret = raw_write8(s->addr, KXCJ9_CTRL1, *ctrl1);
		if (ret == EC_SUCCESS)
			return EC_SUCCESS;
	}
	return ret;
}

/**
 * Enable sensor by placing it in operating mode.
 *
 * Note: This is intended to be called in a pair with disable_sensor().
 *
 * @data Pointer to motion sensor data
 * @ctrl1 Value of KXCJ9_CTRL1 register to write to sensor
 *
 * @return EC_SUCCESS if successful, EC_ERROR_* otherwise
 */
static int enable_sensor(const struct motion_sensor_t *s, int ctrl1)
{
	int i, ret;

	for (i = 0; i < SENSOR_ENABLE_ATTEMPTS; i++) {
		ret = raw_read8(s->addr, KXCJ9_CTRL1, &ctrl1);
		if (ret != EC_SUCCESS)
			continue;

		/* Enable accelerometer based on ctrl1 value. */
		ret = raw_write8(s->addr, KXCJ9_CTRL1,
				ctrl1 | KXCJ9_CTRL1_PC1);

		/* On first success, we are done. */
		if (ret == EC_SUCCESS)
			return EC_SUCCESS;
	}
	return ret;
}

static int set_range(const struct motion_sensor_t *s,
				int range,
				int rnd)
{
	int ret, ctrl1, ctrl1_new, index;
	struct kxcj9_data *data = s->drv_data;

	/* Find index for interface pair matching the specified range. */
	index = find_param_index(range, rnd, ranges, ARRAY_SIZE(ranges));

	/* Disable the sensor to allow for changing of critical parameters. */
	mutex_lock(s->mutex);
	ret = disable_sensor(s, &ctrl1);
	if (ret != EC_SUCCESS) {
		mutex_unlock(s->mutex);
		return ret;
	}

	/* Determine new value of CTRL1 reg and attempt to write it. */
	ctrl1_new = (ctrl1 & ~KXCJ9_GSEL_ALL) | ranges[index].reg;
	ret = raw_write8(s->addr,  KXCJ9_CTRL1, ctrl1_new);

	/* If successfully written, then save the range. */
	if (ret == EC_SUCCESS) {
		data->sensor_range = index;
		ctrl1 = ctrl1_new;
	}

	/* Re-enable the sensor. */
	if (enable_sensor(s, ctrl1) != EC_SUCCESS)
		ret = EC_ERROR_UNKNOWN;

	mutex_unlock(s->mutex);
	return ret;
}

static int get_range(const struct motion_sensor_t *s)
{
	struct kxcj9_data *data = s->drv_data;
	return ranges[data->sensor_range].val;
}

static int set_resolution(const struct motion_sensor_t *s,
				int res,
				int rnd)
{
	int ret, ctrl1, ctrl1_new, index;
	struct kxcj9_data *data = s->drv_data;

	/* Find index for interface pair matching the specified resolution. */
	index = find_param_index(res, rnd, resolutions,
			ARRAY_SIZE(resolutions));

	/* Disable the sensor to allow for changing of critical parameters. */
	mutex_lock(s->mutex);
	ret = disable_sensor(s, &ctrl1);
	if (ret != EC_SUCCESS) {
		mutex_unlock(s->mutex);
		return ret;
	}

	/* Determine new value of CTRL1 reg and attempt to write it. */
	ctrl1_new = (ctrl1 & ~KXCJ9_RES_12BIT) | resolutions[index].reg;
	ret = raw_write8(s->addr,  KXCJ9_CTRL1, ctrl1_new);

	/* If successfully written, then save the range. */
	if (ret == EC_SUCCESS) {
		data->sensor_resolution = index;
		ctrl1 = ctrl1_new;
	}

	/* Re-enable the sensor. */
	if (enable_sensor(s, ctrl1) != EC_SUCCESS)
		ret = EC_ERROR_UNKNOWN;

	mutex_unlock(s->mutex);
	return ret;
}

static int get_resolution(const struct motion_sensor_t *s)
{
	struct kxcj9_data *data = s->drv_data;
	return resolutions[data->sensor_resolution].val;
}

static int set_data_rate(const struct motion_sensor_t *s,
			int rate,
			int rnd)
{
	int ret, ctrl1, index;
	struct kxcj9_data *data = s->drv_data;

	/* Find index for interface pair matching the specified rate. */
	index = find_param_index(rate, rnd, datarates, ARRAY_SIZE(datarates));

	/* Disable the sensor to allow for changing of critical parameters. */
	mutex_lock(s->mutex);
	ret = disable_sensor(s, &ctrl1);
	if (ret != EC_SUCCESS) {
		mutex_unlock(s->mutex);
		return ret;
	}

	/* Set output data rate. */
	ret = raw_write8(s->addr,  KXCJ9_DATA_CTRL,
			datarates[index].reg);

	/* If successfully written, then save the range. */
	if (ret == EC_SUCCESS)
		data->sensor_datarate = index;

	/* Re-enable the sensor. */
	if (enable_sensor(s, ctrl1) != EC_SUCCESS)
		ret = EC_ERROR_UNKNOWN;

	mutex_unlock(s->mutex);
	return ret;
}

static int get_data_rate(const struct motion_sensor_t *s)
{
	struct kxcj9_data *data = s->drv_data;
	return datarates[data->sensor_datarate].val;
}

static int set_offset(const struct motion_sensor_t *s,
			const int16_t *offset,
			int16_t    temp)
{
	/* temperature is ignored */
	struct kxcj9_data *data = s->drv_data;
	data->offset[X] = offset[X];
	data->offset[Y] = offset[Y];
	data->offset[Z] = offset[Z];
	return EC_SUCCESS;
}

static int get_offset(const struct motion_sensor_t *s,
			int16_t   *offset,
			int16_t    *temp)
{
	struct kxcj9_data *data = s->drv_data;
	offset[X] = data->offset[X];
	offset[Y] = data->offset[Y];
	offset[Z] = data->offset[Z];
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

#ifdef CONFIG_ACCEL_INTERRUPTS
static int set_interrupt(const struct motion_sensor_t *s,
		unsigned int threshold)
{
	int ctrl1, tmp, ret;
	struct kxcj9_data *data = s->drv_data;

	/* Disable the sensor to allow for changing of critical parameters. */
	mutex_lock(s->mutex);
	ret = disable_sensor(s, &ctrl1);
	if (ret != EC_SUCCESS) {
		mutex_unlock(s->mutex);
		return ret;
	}

	/* Set interrupt timer to 1 so it wakes up immediately. */
	ret = raw_write8(s->addr, KXCJ9_WAKEUP_TIMER, 1);
	if (ret != EC_SUCCESS)
		goto error_enable_sensor;

	/*
	 * Set threshold, note threshold register is in units of 16 counts, so
	 * first we need to divide by 16 to get the value to send.
	 */
	threshold >>= 4;
	ret = raw_write8(s->addr, KXCJ9_WAKEUP_THRESHOLD, threshold);
	if (ret != EC_SUCCESS)
		goto error_enable_sensor;

	/*
	 * Set interrupt enable register on sensor. Note that once this
	 * function is called once, the interrupt stays enabled and it is
	 * only necessary to clear KXCJ9_INT_REL to allow the next interrupt.
	 */
	ret = raw_read8(s->addr, KXCJ9_INT_CTRL1, &tmp);
	if (ret != EC_SUCCESS)
		goto error_enable_sensor;
	if (!(tmp & KXCJ9_INT_CTRL1_IEN)) {
		ret = raw_write8(s->addr, KXCJ9_INT_CTRL1,
				tmp | KXCJ9_INT_CTRL1_IEN);
		if (ret != EC_SUCCESS)
			goto error_enable_sensor;
	}

	/*
	 * Clear any pending interrupt on sensor by reading INT_REL register.
	 * Note: this register latches motion detected above threshold. Once
	 * latched, no interrupt can occur until this register is cleared.
	 */
	ret = raw_read8(s->addr, KXCJ9_INT_REL, &tmp);

error_enable_sensor:
	/* Re-enable the sensor. */
	if (enable_sensor(s, ctrl1) != EC_SUCCESS)
		ret = EC_ERROR_UNKNOWN;
	mutex_unlock(s->mutex);
	return ret;
}
#endif

static int read(const struct motion_sensor_t *s, vector_3_t v)
{
	uint8_t acc[6];
	uint8_t reg = KXCJ9_XOUT_L;
	int ret, i, range, resolution;
	struct kxcj9_data *data = s->drv_data;

	/* Read 6 bytes starting at KXCJ9_XOUT_L. */
	mutex_lock(s->mutex);
	i2c_lock(I2C_PORT_ACCEL, 1);
	ret = i2c_xfer(I2C_PORT_ACCEL, s->addr, &reg, 1, acc, 6,
			I2C_XFER_SINGLE);
	i2c_lock(I2C_PORT_ACCEL, 0);
	mutex_unlock(s->mutex);

	if (ret != EC_SUCCESS)
		return ret;

	/*
	 * Convert acceleration to a signed 16-bit number. Note, based on
	 * the order of the registers:
	 *
	 * acc[0] = KXCJ9_XOUT_L
	 * acc[1] = KXCJ9_XOUT_H
	 * acc[2] = KXCJ9_YOUT_L
	 * acc[3] = KXCJ9_YOUT_H
	 * acc[4] = KXCJ9_ZOUT_L
	 * acc[5] = KXCJ9_ZOUT_H
	 *
	 * Add calibration offset before returning the data.
	 */
	resolution = get_resolution(s);
	for (i = X; i <= Z; i++) {
		v[i] = (((int8_t)acc[i * 2 + 1]) << 4) |
		       (acc[i * 2] >> 4);
		v[i] <<= (16 - resolution);
	}
	rotate(v, *s->rot_standard_ref, v);

	/* apply offset in the device coordinates */
	range = get_range(s);
	for (i = X; i <= Z; i++)
		v[i] += (data->offset[i] << 5) / range;

	return EC_SUCCESS;
}

#ifdef CONFIG_ACCEL_INTERRUPTS
static int config_interrupt(const struct motion_sensor_t *s)
{
	int ctrl1;
	mutex_lock(s->mutex);

	/* Disable the sensor to allow for changing of critical parameters. */
	ret = disable_sensor(s, &ctrl1);
	if (ret != EC_SUCCESS)
		goto cleanup_exit;

	/* Enable wake up (motion detect) functionality. */
	ret = raw_read8(s->addr, KXCJ9_CTRL1, &tmp);
	tmp &= ~KXCJ9_CTRL1_PC1;
	tmp |= KXCJ9_CTRL1_WUFE;
	ret = raw_write8(s->addr, KXCJ9_CTRL1, tmp);

	/* Set interrupt polarity to rising edge and keep interrupt disabled. */
	ret = raw_write8(s->addr,
			  KXCJ9_INT_CTRL1,
			  KXCJ9_INT_CTRL1_IEA);
	if (ret != EC_SUCCESS)
		goto cleanup_exit;

	/* Set output data rate for wake-up interrupt function. */
	ret = raw_write8(s->addr, KXCJ9_CTRL2, KXCJ9_OWUF_100_0HZ);
	if (ret != EC_SUCCESS)
		goto cleanup_exit;

	/* Set interrupt to trigger on motion on any axis. */
	ret = raw_write8(s->addr, KXCJ9_INT_CTRL2,
			KXCJ9_INT_SRC2_XNWU | KXCJ9_INT_SRC2_XPWU |
			KXCJ9_INT_SRC2_YNWU | KXCJ9_INT_SRC2_YPWU |
			KXCJ9_INT_SRC2_ZNWU | KXCJ9_INT_SRC2_ZPWU);
	if (ret != EC_SUCCESS)
		goto cleanup_exit;

	/*
	 * Enable accel interrupts. Note: accels will not initiate an interrupt
	 * until interrupt enable bit in KXCJ9_INT_CTRL1 is set on the device.
	 */
	gpio_enable_interrupt(GPIO_ACCEL_INT_LID);
	gpio_enable_interrupt(GPIO_ACCEL_INT_BASE);

	/* Enable the sensor. */
	ret = enable_sensor(s, ctrl1);
cleanup_exit:
	mutex_unlock(s->mutex);
	return ret;
}
#endif

static int init(const struct motion_sensor_t *s)
{
	int ret = EC_SUCCESS;
	int cnt = 0, tmp;

	/*
	 * This sensor can be powered through an EC reboot, so the state of
	 * the sensor is unknown here. Initiate software reset to restore
	 * sensor to default.
	 */
	mutex_lock(s->mutex);
	ret = raw_write8(s->addr, KXCJ9_CTRL2, KXCJ9_CTRL2_SRST);
	mutex_unlock(s->mutex);
	if (ret != EC_SUCCESS)
		return ret;

	/* Wait until software reset is complete or timeout. */
	do {
		/* Added 1m delay after software reset */
		msleep(1);

		ret = raw_read8(s->addr, KXCJ9_CTRL2, &tmp);
		if (ret != EC_SUCCESS)
			return ret;

		/* Reset complete. */
		if (ret == EC_SUCCESS && !(tmp & KXCJ9_CTRL2_SRST))
			break;

		/* Check for timeout. */
		if (cnt++ > 5) {
			ret = EC_ERROR_TIMEOUT;
			return ret;
		}
	} while (1);

	ret = set_range(s, s->default_range, 1);
	if (ret != EC_SUCCESS)
		return ret;

	ret = set_resolution(s, 12, 1);
	if (ret != EC_SUCCESS)
		return ret;

#ifdef CONFIG_ACCEL_INTERRUPTS
	config_interrupt(s);
#endif
	CPRINTF("[%T %s: Done Init type:0x%X range:%d]\n",
		s->name, s->type, get_range(s));

	return ret;
}

const struct accelgyro_drv kxcj9_drv = {
	.init = init,
	.read = read,
	.set_range = set_range,
	.get_range = get_range,
	.set_resolution = set_resolution,
	.get_resolution = get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = get_data_rate,
	.set_offset = set_offset,
	.get_offset = get_offset,
	.perform_calib = NULL,
#ifdef CONFIG_ACCEL_INTERRUPTS
	.set_interrupt = set_interrupt,
#endif
};
