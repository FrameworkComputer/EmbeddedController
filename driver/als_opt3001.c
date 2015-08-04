/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI OPT3001 light sensor driver
 */

#include "driver/als_opt3001.h"
#include "i2c.h"

/**
 *  Read register from OPT3001 light sensor.
 */
static int opt3001_i2c_read(const int reg, int *data_ptr)
{
	int ret;

	ret = i2c_read16(I2C_PORT_ALS, OPT3001_I2C_ADDR, reg, data_ptr);
	if (!ret)
		*data_ptr = ((*data_ptr << 8) & 0xFF00) |
				((*data_ptr >> 8) & 0x00FF);

	return ret;
}

/**
 *  Write register to OPT3001 light sensor.
 */
static int opt3001_i2c_write(const int reg, int data)
{
	data = ((data << 8) & 0xFF00) | ((data >> 8) & 0x00FF);
	return i2c_write16(I2C_PORT_ALS, OPT3001_I2C_ADDR, reg, data);
}

/**
 * Initialise OPT3001 light sensor.
 */
int opt3001_init(void)
{
	int data;
	int ret;

	ret = opt3001_i2c_read(OPT3001_REG_MAN_ID, &data);
	if (ret)
		return ret;
	if (data != OPT3001_MANUFACTURER_ID)
		return EC_ERROR_UNKNOWN;

	ret = opt3001_i2c_read(OPT3001_REG_DEV_ID, &data);
	if (ret)
		return ret;
	if (data != OPT3001_DEVICE_ID)
		return EC_ERROR_UNKNOWN;

	/*
	 * [15:12]: 0101b Automatic full scale (1310.40lux, 0.32lux/lsb)
	 * [11]   : 1b    Conversion time 800ms
	 * [10:9] : 10b   Continuous Mode of conversion operation
	 * [4]    : 1b    Latched window-style comparison operation
	 */
	return opt3001_i2c_write(OPT3001_REG_CONFIGURE, 0x5C10);
}

/**
 * Read OPT3001 light sensor data.
 */
int opt3001_read_lux(int *lux, int af)
{
	int ret;
	int data;

	ret = opt3001_i2c_read(OPT3001_REG_RESULT, &data);
	if (ret)
		return ret;

	/*
	 * The default power-on values will give 12 bits of precision:
	 * 0x0000-0x0fff indicates 0 to 1310.40 lux. We multiply the sensor
	 * value by a scaling factor to account for attenuation by glass,
	 * tinting, etc.
	 */

	/*
	 * lux = 2EXP[3:0] × R[11:0] / 100
	 */
	*lux = (1 << ((data & 0xF000) >> 12)) * (data & 0x0FFF) * af / 100;

	return EC_SUCCESS;
}

#ifdef CONFIG_CMD_I2C_STRESS_TEST_ALS
struct i2c_stress_test_dev opt3001_i2c_stress_test_dev = {
	.reg_info = {
		.read_reg = OPT3001_REG_DEV_ID,
		.read_val = OPT3001_DEVICE_ID,
		.write_reg = OPT3001_REG_INT_LIMIT_LSB,
	},
	.i2c_read_dev = &opt3001_i2c_read,
	.i2c_write_dev = &opt3001_i2c_write,
};
#endif /* CONFIG_CMD_I2C_STRESS_TEST_ALS */
