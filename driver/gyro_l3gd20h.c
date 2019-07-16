/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * L3GD20H gyro module for Chrome EC 3D digital gyroscope.
 */

#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "driver/gyro_l3gd20h.h"
#include "hooks.h"
#include "i2c.h"
#include "task.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ## args)

/*
 * Struct for pairing an engineering value with the register value for a
 * parameter.
 */
struct gyro_param_pair {
	int val; /* Value in engineering units. */
	int reg_val; /* Corresponding register value. */
};

/*
 * List of angular rate range values in +/-dps's
 * and their associated register values.
 */
const struct gyro_param_pair dps_ranges[] = {
	{245, L3GD20_DPS_SEL_245},
	{500, L3GD20_DPS_SEL_500},
	{2000, L3GD20_DPS_SEL_2000_0},
	{2000, L3GD20_DPS_SEL_2000_1}
};

static inline const struct gyro_param_pair *get_range_table(
		enum motionsensor_type type, int *psize)
{
	if (psize)
		*psize = ARRAY_SIZE(dps_ranges);
	return dps_ranges;
}

/* List of ODR values in mHz and their associated register values. */
const struct gyro_param_pair gyro_odr[] = {
	{0,      L3GD20_ODR_PD | L3GD20_LOW_ODR_MASK},
	{12500,  L3GD20_ODR_12_5HZ | L3GD20_ODR_PD_MASK | L3GD20_LOW_ODR_MASK},
	{25000,  L3GD20_ODR_25HZ | L3GD20_ODR_PD_MASK | L3GD20_LOW_ODR_MASK},
	{50000,  L3GD20_ODR_50HZ_0 | L3GD20_ODR_PD_MASK | L3GD20_LOW_ODR_MASK},
	{50000,  L3GD20_ODR_50HZ_1 | L3GD20_ODR_PD_MASK | L3GD20_LOW_ODR_MASK},
	{100000, L3GD20_ODR_100HZ | L3GD20_ODR_PD_MASK},
	{200000, L3GD20_ODR_200HZ | L3GD20_ODR_PD_MASK},
	{400000, L3GD20_ODR_400HZ | L3GD20_ODR_PD_MASK},
	{800000, L3GD20_ODR_800HZ | L3GD20_ODR_PD_MASK},
};

static inline const struct gyro_param_pair *get_odr_table(
		enum motionsensor_type type, int *psize)
{
	if (psize)
		*psize = ARRAY_SIZE(gyro_odr);
	return gyro_odr;
}

static inline int get_ctrl_reg(enum motionsensor_type type)
{
	return L3GD20_CTRL_REG1;
}

static inline int get_xyz_reg(enum motionsensor_type type)
{
	return L3GD20_OUT_X_L | BIT(7);
}

/**
 * @return reg value that matches the given engineering value passed in.
 * The round_up flag is used to specify whether to round up or down.
 * Note, this function always returns a valid reg value. If the request is
 * outside the range of values, it returns the closest valid reg value.
 */
static int get_reg_val(const int eng_val, const int round_up,
		const struct gyro_param_pair *pairs, const int size)
{
	int i;
	for (i = 0; i < size - 1; i++) {
		if (eng_val <= pairs[i].val)
			break;

		if (eng_val < pairs[i+1].val) {
			if (round_up)
				i += 1;
			break;
		}
	}
	return pairs[i].reg_val;
}

/**
 * @return engineering value that matches the given reg val
 */
static int get_engineering_val(const int reg_val,
		const struct gyro_param_pair *pairs, const int size)
{
	int i;
	for (i = 0; i < size; i++) {
		if (reg_val == pairs[i].reg_val)
			break;
	}
	return pairs[i].val;
}

/**
 * Read register from Gyrometer.
 */
static inline int raw_read8(const int port, const int addr, const int reg,
							int *data_ptr)
{
	return i2c_read8(port, addr, reg, data_ptr);
}

/**
 * Write register from Gyrometer.
 */
static inline int raw_write8(const int port, const int addr, const int reg,
							 int data)
{
	return i2c_write8(port, addr, reg, data);
}

static int set_range(const struct motion_sensor_t *s,
				int range,
				int rnd)
{
	int ret, ctrl_val, range_tbl_size;
	uint8_t ctrl_reg, reg_val;
	const struct gyro_param_pair *ranges;
	struct l3gd20_data *data = (struct l3gd20_data *)s->drv_data;

	ctrl_reg = L3GD20_CTRL_REG4;
	ranges = get_range_table(s->type, &range_tbl_size);

	reg_val = get_reg_val(range, rnd, ranges, range_tbl_size);

	/*
	 * Lock Gyro resource to prevent another task from attempting
	 * to write Gyro parameters until we are done.
	 */
	mutex_lock(s->mutex);

	ret = raw_read8(s->port, s->addr, ctrl_reg, &ctrl_val);
	if (ret != EC_SUCCESS)
		goto gyro_cleanup;

	ctrl_val = (ctrl_val & ~L3GD20_RANGE_MASK) | reg_val;
	ret = raw_write8(s->port, s->addr, ctrl_reg, ctrl_val);

	/* Now that we have set the range, update the driver's value. */
	if (ret == EC_SUCCESS)
		data->base.range = get_engineering_val(reg_val, ranges,
							 range_tbl_size);

gyro_cleanup:
	mutex_unlock(s->mutex);
	return EC_SUCCESS;
}

static int get_range(const struct motion_sensor_t *s)
{
	struct l3gd20_data *data = (struct l3gd20_data *)s->drv_data;

	return data->base.range;
}

static int get_resolution(const struct motion_sensor_t *s)
{
	return L3GD20_RESOLUTION;
}

static int set_data_rate(const struct motion_sensor_t *s,
				int rate,
				int rnd)
{
	int ret, val, odr_tbl_size;
	uint8_t ctrl_reg, reg_val;
	const struct gyro_param_pair *data_rates;
	struct l3gd20_data *data = s->drv_data;

	ctrl_reg = get_ctrl_reg(s->type);
	data_rates = get_odr_table(s->type, &odr_tbl_size);
	reg_val = get_reg_val(rate, rnd, data_rates, odr_tbl_size);

	/*
	 * Lock gyro resource to prevent another task from attempting
	 * to write gyro parameters until we are done.
	 */
	mutex_lock(s->mutex);

	ret = raw_read8(s->port, s->addr, ctrl_reg, &val);
	if (ret != EC_SUCCESS)
		goto gyro_cleanup;

	val = (val & ~(L3GD20_ODR_MASK | L3GD20_ODR_PD_MASK)) |
		(reg_val & ~L3GD20_LOW_ODR_MASK);
	ret = raw_write8(s->port, s->addr, ctrl_reg, val);

	/* Now that we have set the odr, update the driver's value. */
	if (ret == EC_SUCCESS)
		data->base.odr = get_engineering_val(reg_val, data_rates,
						       odr_tbl_size);

	ret = raw_read8(s->port, s->addr, L3GD20_LOW_ODR, &val);
	if (ret != EC_SUCCESS)
		goto gyro_cleanup;

	/* We need to clear low_ODR bit for higher data rates */
	if (reg_val & L3GD20_LOW_ODR_MASK)
		val |= 1;
	else
		val &= ~1;

	ret = raw_write8(s->port, s->addr, L3GD20_LOW_ODR, val);
	if (ret != EC_SUCCESS)
		goto gyro_cleanup;

	/* CTRL_REG5 24h
	 * [7] low-power mode = 0;
	 * [6] fifo disabled = 0;
	 * [5] Stop on fth = 0;
	 * [4] High pass filter enable = 1;
	 * [3:2] int1_sel = 0;
	 * [1:0] out_sel = 1;
	 */
	ret = raw_read8(s->port, s->addr, L3GD20_CTRL_REG5, &val);
	if (ret != EC_SUCCESS)
		goto gyro_cleanup;

	val |= BIT(4); /* high-pass filter enabled */
	val |= BIT(0); /* data in data reg are high-pass filtered */
	ret = raw_write8(s->port, s->addr, L3GD20_CTRL_REG5, val);
	if (ret != EC_SUCCESS)
		goto gyro_cleanup;

	ret = raw_read8(s->port, s->addr, L3GD20_CTRL_REG2, &val);
	if (ret != EC_SUCCESS)
		goto gyro_cleanup;

	/*
	 * Table 25. High pass filter mode configuration
	 * Table 26. High pass filter cut off frequency configuration
	 */
	val &= 0xf0;
	val |= 0x04;
	ret = raw_write8(s->port, s->addr, L3GD20_CTRL_REG2, val);

gyro_cleanup:
	mutex_unlock(s->mutex);
	return ret;
}

static int get_data_rate(const struct motion_sensor_t *s)
{
	struct l3gd20_data *data = (struct l3gd20_data *)s->drv_data;

	return data->base.odr;
}

static int set_offset(const struct motion_sensor_t *s,
			const int16_t *offset,
			int16_t    temp)
{
	/* temperature is ignored */
	struct l3gd20_data *data = s->drv_data;
	data->offset[X] = offset[X];
	data->offset[Y] = offset[Y];
	data->offset[Z] = offset[Z];
	return EC_SUCCESS;
}

static int get_offset(const struct motion_sensor_t *s,
			int16_t    *offset,
			int16_t    *temp)
{
	struct l3gd20_data *data = s->drv_data;
	offset[X] = data->offset[X];
	offset[Y] = data->offset[Y];
	offset[Z] = data->offset[Z];
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static int is_data_ready(const struct motion_sensor_t *s, int *ready)
{
	int ret, tmp;

	ret = raw_read8(s->port, s->addr, L3GD20_STATUS_REG, &tmp);

	if (ret != EC_SUCCESS) {
		CPRINTS("%s type:0x%X RS Error", s->name, s->type);
		return ret;
	}

	*ready = (tmp & L3GD20_STS_ZYXDA_MASK) ? 1 : 0;

	return EC_SUCCESS;
}

static int read(const struct motion_sensor_t *s, intv3_t v)
{
	uint8_t raw[6];
	uint8_t xyz_reg;
	int ret, range, i, tmp = 0;
	struct l3gd20_data *data = s->drv_data;

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

	/* Read 6 bytes starting at xyz_reg */
	i2c_block_read(s->port, s->addr, xyz_reg, raw, 6);

	if (ret != EC_SUCCESS) {
		CPRINTS("%s type:0x%X RD XYZ Error", s->name, s->type);
		return ret;
	}

	for (i = X; i <= Z; i++)
		v[i] = ((int16_t)((raw[i * 2 + 1] << 8) | raw[i * 2]));

	rotate(v, *s->rot_standard_ref, v);

	/* apply offset in the device coordinates */
	range = get_range(s);
	for (i = X; i <= Z; i++)
		v[i] += (data->offset[i] << 5) / range;

	return EC_SUCCESS;
}

static int init(const struct motion_sensor_t *s)
{
	int ret = 0, tmp;

	ret = raw_read8(s->port, s->addr, L3GD20_WHO_AM_I_REG, &tmp);
	if (ret)
		return ret;

	if (tmp != L3GD20_WHO_AM_I)
		return EC_ERROR_ACCESS_DENIED;

	/* All axes are enabled */
	ret = raw_write8(s->port, s->addr, L3GD20_CTRL_REG1, 0x0f);
	if (ret)
		return ret;

	mutex_lock(s->mutex);
	ret = raw_read8(s->port, s->addr, L3GD20_CTRL_REG4, &tmp);
	if (ret) {
		mutex_unlock(s->mutex);
		return ret;
	}

	tmp |= L3GD20_BDU_ENABLE;
	ret = raw_write8(s->port, s->addr, L3GD20_CTRL_REG4, tmp);
	mutex_unlock(s->mutex);
	if (ret)
		return ret;

	return sensor_init_done(s);
}

const struct accelgyro_drv l3gd20h_drv = {
	.init = init,
	.read = read,
	.set_range = set_range,
	.get_range = get_range,
	.get_resolution = get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = get_data_rate,
	.set_offset = set_offset,
	.get_offset = get_offset,
};
