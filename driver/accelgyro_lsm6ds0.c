/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LSM6DS0 accelerometer and gyro module for Chrome EC */

#include "accelerometer.h"
#include "common.h"
#include "console.h"
#include "driver/accelgyro_lsm6ds0.h"
#include "hooks.h"
#include "i2c.h"
#include "task.h"
#include "util.h"

/*
 * Struct for pairing an engineering value with the register value for a
 * parameter.
 */
struct accel_param_pair {
	int val; /* Value in engineering units. */
	int reg; /* Corresponding register value. */
};

/* List of range values in +/-G's and their associated register values. */
const struct accel_param_pair ranges[] = {
	{2, LSM6DS0_GSEL_2G},
	{4, LSM6DS0_GSEL_4G},
	{8, LSM6DS0_GSEL_8G}
};

/* List of ODR values in mHz and their associated register values. */
const struct accel_param_pair datarates[] = {
	{10000,    LSM6DS0_ODR_10HZ},
	{50000,    LSM6DS0_ODR_50HZ},
	{119000,   LSM6DS0_ODR_119HZ},
	{238000,   LSM6DS0_ODR_238HZ},
	{476000,   LSM6DS0_ODR_476HZ},
	{952000,   LSM6DS0_ODR_982HZ}
};

/* Current range of each accelerometer. The value is an index into ranges[]. */
static int sensor_range[ACCEL_COUNT] = {0, 0};

/*
 * Current output data rate of each accelerometer. The value is an index into
 * datarates[].
 */
static int sensor_datarate[ACCEL_COUNT] = {1, 1};

static struct mutex accel_mutex[ACCEL_COUNT];

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

int accel_set_range(const enum accel_id id, const int range, const int rnd)
{
	int ret, index, ctrl_reg6;

	/* Find index for interface pair matching the specified range. */
	index = find_param_index(range, rnd, ranges, ARRAY_SIZE(ranges));

	/*
	 * Lock accel resource to prevent another task from attempting
	 * to write accel parameters until we are done.
	 */
	mutex_lock(&accel_mutex[id]);

	ret = raw_read8(accel_addr[id], LSM6DS0_CTRL_REG6_XL, &ctrl_reg6);
	if (ret != EC_SUCCESS)
		goto accel_cleanup;

	ctrl_reg6 = (ctrl_reg6 & ~LSM6DS0_GSEL_ALL) | ranges[index].reg;
	ret = raw_write8(accel_addr[id], LSM6DS0_CTRL_REG6_XL, ctrl_reg6);

accel_cleanup:
	/* Unlock accel resource and save new range if written successfully. */
	mutex_unlock(&accel_mutex[id]);
	if (ret == EC_SUCCESS)
		sensor_range[id] = index;

	return EC_SUCCESS;
}

int accel_get_range(const enum accel_id id, int * const range)
{
	/* Check for valid id. */
	if (id < 0 || id >= ACCEL_COUNT)
		return EC_ERROR_INVAL;

	*range = ranges[sensor_range[id]].val;
	return EC_SUCCESS;
}

int accel_set_resolution(const enum accel_id id, const int res, const int rnd)
{
	/* Check for valid id. */
	if (id < 0 || id >= ACCEL_COUNT)
		return EC_ERROR_INVAL;

	/* Only one resolution, LSM6DS0_RESOLUTION, so nothing to do. */
	return EC_SUCCESS;
}

int accel_get_resolution(const enum accel_id id, int * const res)
{
	/* Check for valid id. */
	if (id < 0 || id >= ACCEL_COUNT)
		return EC_ERROR_INVAL;

	*res = LSM6DS0_RESOLUTION;
	return EC_SUCCESS;
}

int accel_set_datarate(const enum accel_id id, const int rate, const int rnd)
{
	int ret, index, ctrl_reg6;

	/* Check for valid id. */
	if (id < 0 || id >= ACCEL_COUNT)
		return EC_ERROR_INVAL;

	/* Find index for interface pair matching the specified range. */
	index = find_param_index(rate, rnd, datarates, ARRAY_SIZE(datarates));

	/*
	 * Lock accel resource to prevent another task from attempting
	 * to write accel parameters until we are done.
	 */
	mutex_lock(&accel_mutex[id]);

	ret = raw_read8(accel_addr[id], LSM6DS0_CTRL_REG6_XL, &ctrl_reg6);
	if (ret != EC_SUCCESS)
		goto accel_cleanup;

	ctrl_reg6 = (ctrl_reg6 & ~LSM6DS0_ODR_ALL) | datarates[index].reg;
	ret = raw_write8(accel_addr[id], LSM6DS0_CTRL_REG6_XL, ctrl_reg6);

accel_cleanup:
	/* Unlock accel resource and save new ODR if written successfully. */
	mutex_unlock(&accel_mutex[id]);
	if (ret == EC_SUCCESS)
		sensor_datarate[id] = index;

	return EC_SUCCESS;
}

int accel_get_datarate(const enum accel_id id, int * const rate)
{
	/* Check for valid id. */
	if (id < 0 || id >= ACCEL_COUNT)
		return EC_ERROR_INVAL;

	*rate = datarates[sensor_datarate[id]].val;
	return EC_SUCCESS;
}

#ifdef CONFIG_ACCEL_INTERRUPTS
int accel_set_interrupt(const enum accel_id id, unsigned int threshold)
{
	/* Currently unsupported. */
	return EC_ERROR_UNKNOWN;
}
#endif

int accel_read(const enum accel_id id, int * const x_acc, int * const y_acc,
		int * const z_acc)
{
	uint8_t acc[6];
	uint8_t reg = LSM6DS0_OUT_X_L_XL;
	int ret, multiplier;

	/* Read 6 bytes starting at LSM6DS0_OUT_X_L_XL. */
	mutex_lock(&accel_mutex[id]);
	i2c_lock(I2C_PORT_ACCEL, 1);
	ret = i2c_xfer(I2C_PORT_ACCEL, accel_addr[id], &reg, 1, acc, 6,
			I2C_XFER_SINGLE);
	i2c_lock(I2C_PORT_ACCEL, 0);
	mutex_unlock(&accel_mutex[id]);

	if (ret != EC_SUCCESS)
		return ret;

	/* Determine multiplier based on stored range. */
	switch (ranges[sensor_range[id]].reg) {
	case LSM6DS0_GSEL_2G:
		multiplier = 1;
		break;
	case LSM6DS0_GSEL_4G:
		multiplier = 2;
		break;
	case LSM6DS0_GSEL_8G:
		multiplier = 4;
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}

	/*
	 * Convert data to signed 12-bit value. Note order of registers:
	 *
	 * acc[0] = LSM6DS0_OUT_X_L_XL
	 * acc[1] = LSM6DS0_OUT_X_H_XL
	 * acc[2] = LSM6DS0_OUT_Y_L_XL
	 * acc[3] = LSM6DS0_OUT_Y_H_XL
	 * acc[4] = LSM6DS0_OUT_Z_L_XL
	 * acc[5] = LSM6DS0_OUT_Z_H_XL
	 */
	*x_acc = multiplier * ((int16_t)(acc[1] << 8 | acc[0])) >> 4;
	*y_acc = multiplier * ((int16_t)(acc[3] << 8 | acc[2])) >> 4;
	*z_acc = multiplier * ((int16_t)(acc[5] << 8 | acc[4])) >> 4;

	return EC_SUCCESS;
}

int accel_init(const enum accel_id id)
{
	int ret, ctrl_reg6;

	/* Check for valid id. */
	if (id < 0 || id >= ACCEL_COUNT)
		return EC_ERROR_INVAL;

	mutex_lock(&accel_mutex[id]);

	/*
	 * This sensor can be powered through an EC reboot, so the state of
	 * the sensor is unknown here. Initiate software reset to restore
	 * sensor to default.
	 */
	ret = raw_write8(accel_addr[id], LSM6DS0_CTRL_REG8, 1);
	if (ret != EC_SUCCESS)
		goto accel_cleanup;

	/* Set ODR and range. */
	ctrl_reg6 = datarates[sensor_datarate[id]].reg |
			ranges[sensor_range[id]].reg;

	ret = raw_write8(accel_addr[id], LSM6DS0_CTRL_REG6_XL, ctrl_reg6);

accel_cleanup:
	mutex_unlock(&accel_mutex[id]);
	return ret;
}
