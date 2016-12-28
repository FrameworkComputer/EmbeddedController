/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI OPT3001 light sensor driver
 */

#include "driver/als_opt3001.h"
#include "i2c.h"

#ifdef HAS_TASK_ALS
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
#endif  /* CONFIG_CMD_I2C_STRESS_TEST_ALS */
#else  /* HAS_TASK_ALS */
#include "accelgyro.h"
#include "math_util.h"

/**
 *  Read register from OPT3001 light sensor.
 */
static int opt3001_i2c_read(const int port, const int addr, const int reg,
			    int *data_ptr)
{
	int ret;

	ret = i2c_read16(port, addr, reg, data_ptr);
	if (!ret)
		*data_ptr = ((*data_ptr << 8) & 0xFF00) |
				((*data_ptr >> 8) & 0x00FF);

	return ret;
}

/**
 *  Write register to OPT3001 light sensor.
 */
static int opt3001_i2c_write(const int port, const int addr, const int reg,
			     int data)
{
	data = ((data << 8) & 0xFF00) | ((data >> 8) & 0x00FF);
	return i2c_write16(port, addr, reg, data);
}

/**
 * Read OPT3001 light sensor data.
 */
int opt3001_read_lux(const struct motion_sensor_t *s, vector_3_t v)
{
	struct opt3001_drv_data_t *drv_data = OPT3001_GET_DATA(s);
	int ret;
	int data;

	ret = opt3001_i2c_read(s->port, s->addr, OPT3001_REG_RESULT, &data);
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
	v[0] = ((1 << ((data & 0xF000) >> 12)) * (data & 0x0FFF) *
		drv_data->attenuation) / 100;
	v[1] = 0;
	v[2] = 0;

	/*
	 * Return an error when nothing change to prevent filling the
	 * fifo with useless data.
	 */
	if (v[0] == drv_data->last_value)
		return EC_ERROR_UNCHANGED;
	else
		return EC_SUCCESS;
}

static int opt3001_set_range(const struct motion_sensor_t *s, int range,
			     int rnd)
{
	int rv;
	int reg;
	struct opt3001_drv_data_t *drv_data = OPT3001_GET_DATA(s);

	if (range < 0 || range > OPT3001_RANGE_AUTOMATIC_FULL_SCALE)
		return EC_ERROR_INVAL;

	rv = opt3001_i2c_read(s->port, s->addr, OPT3001_REG_CONFIGURE, &reg);
	if (rv)
		return rv;

	rv = opt3001_i2c_write(s->port, s->addr, OPT3001_REG_CONFIGURE,
			       (reg & OPT3001_RANGE_MASK) |
			       (range << OPT3001_RANGE_OFFSET));
	if (rv)
		return rv;

	drv_data->range = range;

	return EC_SUCCESS;
}

static int opt3001_get_range(const struct motion_sensor_t *s)
{
	struct opt3001_drv_data_t *drv_data = OPT3001_GET_DATA(s);

	return drv_data->range;
}

static int opt3001_set_data_rate(const struct motion_sensor_t *s,
				int rate, int roundup)
{
	struct opt3001_drv_data_t *drv_data = OPT3001_GET_DATA(s);
	int rv;
	int reg;
	enum opt3001_mode mode;

	if (rate == 0) {
		/*
		 * Suspend driver:
		 */
		mode = OPT3001_MODE_SUSPEND;
	} else {
		mode = OPT3001_MODE_CONTINUOUS;
		/*
		 * We set the sensor for continuous mode,
		 * integrating over 800ms.
		 * Do not allow range higher than 1Hz.
		 */
		if (rate > 1000)
			rate = 1000;
	}
	rv = opt3001_i2c_read(s->port, s->addr, OPT3001_REG_CONFIGURE, &reg);
	if (rv)
		return rv;

	rv = opt3001_i2c_write(s->port, s->addr, OPT3001_REG_CONFIGURE,
			       (reg & OPT3001_MODE_MASK) |
			       (mode << OPT3001_MODE_OFFSET));
	if (rv)
		return rv;

	drv_data->rate = rate;
	return EC_SUCCESS;
}

static int opt3001_get_data_rate(const struct motion_sensor_t *s)
{
	struct opt3001_drv_data_t *drv_data = OPT3001_GET_DATA(s);

	return drv_data->rate;
}

static int opt3001_set_offset(const struct motion_sensor_t *s,
			const int16_t *offset,
			int16_t    temp)
{
	return EC_RES_INVALID_COMMAND;
}

static int opt3001_get_offset(const struct motion_sensor_t *s,
			int16_t   *offset,
			int16_t    *temp)
{
	return EC_RES_INVALID_COMMAND;
}
/**
 * Initialise OPT3001 light sensor.
 */
static int opt3001_init(const struct motion_sensor_t *s)
{
	int data;
	int ret;

	ret = opt3001_i2c_read(s->port, s->addr, OPT3001_REG_MAN_ID, &data);
	if (ret)
		return ret;
	if (data != OPT3001_MANUFACTURER_ID)
		return EC_ERROR_ACCESS_DENIED;

	ret = opt3001_i2c_read(s->port, s->addr, OPT3001_REG_DEV_ID, &data);
	if (ret)
		return ret;
	if (data != OPT3001_DEVICE_ID)
		return EC_ERROR_ACCESS_DENIED;

	/*
	 * [11]   : 1b    Conversion time 800ms
	 * [4]    : 1b    Latched window-style comparison operation
	 */
	 opt3001_i2c_write(s->port, s->addr, OPT3001_REG_CONFIGURE, 0x810);
	 return opt3001_set_range(s, s->default_range, 0);
}

const struct accelgyro_drv opt3001_drv = {
	.init = opt3001_init,
	.read = opt3001_read_lux,
	.set_range = opt3001_set_range,
	.get_range = opt3001_get_range,
	.set_offset = opt3001_set_offset,
	.get_offset = opt3001_get_offset,
	.set_data_rate = opt3001_set_data_rate,
	.get_data_rate = opt3001_get_data_rate,
};

#ifdef CONFIG_CMD_I2C_STRESS_TEST_ALS
struct i2c_stress_test_dev opt3001_i2c_stress_test_dev = {
	.reg_info = {
		.read_reg = OPT3001_REG_DEV_ID,
		.read_val = OPT3001_DEVICE_ID,
		.write_reg = OPT3001_REG_INT_LIMIT_LSB,
	},
	.i2c_read = &opt3001_i2c_read,
	.i2c_write = &opt3001_i2c_write,
};
#endif  /* CONFIG_CMD_I2C_STRESS_TEST_ALS */
#endif  /* HAS_TASK_ALS */
