/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* KXCJ9 gsensor module for Chrome EC */

#include "accelerometer.h"
#include "common.h"
#include "console.h"
#include "driver/accel_kxcj9.h"
#include "gpio.h"
#include "i2c.h"
#include "timer.h"
#include "util.h"

/* Range of the accelerometers: 2G, 4G, or 8G. */
static int sensor_range[ACCEL_COUNT] = {KXCJ9_GSEL_2G, KXCJ9_GSEL_2G};

/* Resolution: KXCJ9_RES_12BIT or KXCJ9_RES_12BIT. */
static int sensor_resolution[ACCEL_COUNT] = {KXCJ9_RES_12BIT, KXCJ9_RES_12BIT};

/* Output data rate: KXCJ9_OSA_* ranges from 0.781Hz to 1600Hz. */
static int sensor_datarate[ACCEL_COUNT] = {KXCJ9_OSA_12_50HZ,
						KXCJ9_OSA_12_50HZ};

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


int accel_write_range(const enum accel_id id, const int range)
{
	int ret;

	/* Check for valid id. */
	if (id < 0 || id >= ACCEL_COUNT)
		return EC_ERROR_INVAL;

	/*
	 * Verify that the input range is valid. Note that we currently
	 * don't support the 8G with 14-bit resolution mode.
	 */
	if (range != KXCJ9_GSEL_2G && range != KXCJ9_GSEL_4G &&
		range != KXCJ9_GSEL_8G)
		return EC_ERROR_INVAL;

	ret = raw_write8(accel_addr[id],  KXCJ9_CTRL1,
			KXCJ9_CTRL1_PC1 | sensor_resolution[id] | range);

	/* If successfully written, then save the range. */
	if (ret == EC_SUCCESS)
		sensor_range[id] = range;

	return ret;
}

int accel_write_resolution(const enum accel_id id, const int res)
{
	int ret;

	/* Check for valid id. */
	if (id < 0 || id >= ACCEL_COUNT)
		return EC_ERROR_INVAL;

	/* Check that resolution input is valid. */
	if (res != KXCJ9_RES_12BIT && res != KXCJ9_RES_8BIT)
		return EC_ERROR_INVAL;

	ret = raw_write8(accel_addr[id],  KXCJ9_CTRL1,
			KXCJ9_CTRL1_PC1 | res | sensor_range[id]);

	/* If successfully written, then save the range. */
	if (ret == EC_SUCCESS)
		sensor_resolution[id] = res;

	return ret;
}

int accel_write_datarate(const enum accel_id id, const int rate)
{
	int ret;

	/* Check for valid id. */
	if (id < 0 || id >= ACCEL_COUNT)
		return EC_ERROR_INVAL;

	/* Check that rate input is valid. */
	if (rate < KXCJ9_OSA_12_50HZ || rate > KXCJ9_OSA_6_250HZ)
		return EC_ERROR_INVAL;

	/* Set output data rate. */
	ret = raw_write8(accel_addr[id],  KXCJ9_DATA_CTRL, rate);

	/* If successfully written, then save the range. */
	if (ret == EC_SUCCESS)
		sensor_datarate[id] = rate;

	return ret;
}

int accel_read(enum accel_id id, int *x_acc, int *y_acc, int *z_acc)
{
	uint8_t acc[6];
	uint8_t reg = KXCJ9_XOUT_L;
	int ret, multiplier;

	/* Check for valid id. */
	if (id < 0 || id >= ACCEL_COUNT)
		return EC_ERROR_INVAL;

	/* Read 6 bytes starting at KXCJ9_XOUT_L. */
	i2c_lock(I2C_PORT_ACCEL, 1);
	ret = i2c_xfer(I2C_PORT_ACCEL, accel_addr[id], &reg, 1, acc, 6,
			I2C_XFER_SINGLE);
	i2c_lock(I2C_PORT_ACCEL, 0);

	if (ret != EC_SUCCESS)
		return ret;

	/* Determine multiplier based on stored range. */
	switch (sensor_range[id]) {
	case KXCJ9_GSEL_2G:
		multiplier = 1;
		break;
	case KXCJ9_GSEL_4G:
		multiplier = 2;
		break;
	case KXCJ9_GSEL_8G:
		multiplier = 4;
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}

	/*
	 * Convert acceleration to a signed 12-bit number. Note, based on
	 * the order of the registers:
	 *
	 * acc[0] = KXCJ9_XOUT_L
	 * acc[1] = KXCJ9_XOUT_H
	 * acc[2] = KXCJ9_YOUT_L
	 * acc[3] = KXCJ9_YOUT_H
	 * acc[4] = KXCJ9_ZOUT_L
	 * acc[5] = KXCJ9_ZOUT_H
	 */
	*x_acc = multiplier * (((int8_t)acc[1]) << 4) | (acc[0] >> 4);
	*y_acc = multiplier * (((int8_t)acc[3]) << 4) | (acc[2] >> 4);
	*z_acc = multiplier * (((int8_t)acc[5]) << 4) | (acc[4] >> 4);

	return EC_SUCCESS;
}

int accel_init(enum accel_id id)
{
	int ret = EC_SUCCESS;
	int cnt = 0, ctrl2;

	/* Check for valid id. */
	if (id < 0 || id >= ACCEL_COUNT)
		return EC_ERROR_INVAL;

	/*
	 * This sensor can be powered through an EC reboot, so the state of
	 * the sensor is unknown here. Initiate software reset to restore
	 * sensor to default.
	 */
	ret = raw_write8(accel_addr[id], KXCJ9_CTRL2, KXCJ9_CTRL2_SRST);
	if (ret != EC_SUCCESS)
		return ret;

	/* Wait until software reset is complete or timeout. */
	while (1) {
		raw_read8(accel_addr[id], KXCJ9_CTRL2, &ctrl2);

		/* Reset complete. */
		if (!(ctrl2 & KXCJ9_CTRL2_SRST))
			break;

		/* Check for timeout. */
		if (cnt++ > 5)
			return EC_ERROR_TIMEOUT;

		/* Give more time for reset action to complete. */
		msleep(10);
	}

	/* Enable accelerometer, 12-bit resolution mode, +/- 2G range.*/
	ret |= raw_write8(accel_addr[id],  KXCJ9_CTRL1,
			KXCJ9_CTRL1_PC1 | sensor_resolution[id] |
			sensor_range[id]);

	/* Set output data rate. */
	ret |= raw_write8(accel_addr[id],  KXCJ9_DATA_CTRL,
			sensor_datarate[id]);

	return ret;
}



/*****************************************************************************/
/* Console commands */

#ifdef CONFIG_CMD_ACCELS
static int command_read_accelerometer(int argc, char **argv)
{
	char *e;
	int addr, reg, data;

	if (argc != 3)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is address. */
	addr = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	/* Second argument is register offset. */
	reg = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	raw_read8(addr, reg, &data);

	ccprintf("0x%02x\n", data);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelread, command_read_accelerometer,
	"addr reg",
	"Read from accelerometer at slave address addr", NULL);

static int command_write_accelerometer(int argc, char **argv)
{
	char *e;
	int addr, reg, data;

	if (argc != 4)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is address. */
	addr = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	/* Second argument is register offset. */
	reg = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	/* Third argument is data. */
	data = strtoi(argv[3], &e, 0);
	if (*e)
		return EC_ERROR_PARAM3;

	raw_write8(addr, reg, data);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelwrite, command_write_accelerometer,
	"addr reg data",
	"Write to accelerometer at slave address addr", NULL);
#endif /* CONFIG_CMD_ACCELS */
