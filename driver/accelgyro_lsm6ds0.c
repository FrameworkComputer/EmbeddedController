/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * LSM6DS0 accelerometer and gyro module for Chrome EC
 * 3D digital accelerometer & 3D digital gyroscope
 */

#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "driver/accelgyro_lsm6ds0.h"
#include "hooks.h"
#include "i2c.h"
#include "math_util.h"
#include "task.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ##args)

/*
 * Struct for pairing an engineering value with the register value for a
 * parameter.
 */
struct accel_param_pair {
	int val; /* Value in engineering units. */
	int reg_val; /* Corresponding register value. */
};

/* List of range values in +/-G's and their associated register values. */
static const struct accel_param_pair g_ranges[] = { { 2, LSM6DS0_GSEL_2G },
						    { 4, LSM6DS0_GSEL_4G },
						    { 8, LSM6DS0_GSEL_8G } };

/*
 * List of angular rate range values in +/-dps's
 * and their associated register values.
 */
const struct accel_param_pair dps_ranges[] = { { 245, LSM6DS0_DPS_SEL_245 },
					       { 500, LSM6DS0_DPS_SEL_500 },
					       { 1000, LSM6DS0_DPS_SEL_1000 },
					       { 2000, LSM6DS0_DPS_SEL_2000 } };

static inline const struct accel_param_pair *
get_range_table(enum motionsensor_type type, int *psize)
{
	if (MOTIONSENSE_TYPE_ACCEL == type) {
		if (psize)
			*psize = ARRAY_SIZE(g_ranges);
		return g_ranges;
	} else {
		if (psize)
			*psize = ARRAY_SIZE(dps_ranges);
		return dps_ranges;
	}
}

/* List of ODR (gyro off) values in mHz and their associated register values.*/
const struct accel_param_pair gyro_on_odr[] = {
	{ 0, LSM6DS0_ODR_PD },	       { 15000, LSM6DS0_ODR_15HZ },
	{ 59000, LSM6DS0_ODR_59HZ },   { 119000, LSM6DS0_ODR_119HZ },
	{ 238000, LSM6DS0_ODR_238HZ }, { 476000, LSM6DS0_ODR_476HZ },
	{ 952000, LSM6DS0_ODR_952HZ }
};

/* List of ODR (gyro on) values in mHz and their associated register values. */
const struct accel_param_pair gyro_off_odr[] = {
	{ 0, LSM6DS0_ODR_PD },	       { 10000, LSM6DS0_ODR_10HZ },
	{ 50000, LSM6DS0_ODR_50HZ },   { 119000, LSM6DS0_ODR_119HZ },
	{ 238000, LSM6DS0_ODR_238HZ }, { 476000, LSM6DS0_ODR_476HZ },
	{ 952000, LSM6DS0_ODR_952HZ }
};

static inline const struct accel_param_pair *
get_odr_table(enum motionsensor_type type, int *psize)
{
	if (MOTIONSENSE_TYPE_ACCEL == type) {
		if (psize)
			*psize = ARRAY_SIZE(gyro_off_odr);
		return gyro_off_odr;
	} else {
		if (psize)
			*psize = ARRAY_SIZE(gyro_on_odr);
		return gyro_on_odr;
	}
}

static inline int get_ctrl_reg(enum motionsensor_type type)
{
	return (MOTIONSENSE_TYPE_ACCEL == type) ? LSM6DS0_CTRL_REG6_XL :
						  LSM6DS0_CTRL_REG1_G;
}

static inline int get_xyz_reg(enum motionsensor_type type)
{
	return (MOTIONSENSE_TYPE_ACCEL == type) ? LSM6DS0_OUT_X_L_XL :
						  LSM6DS0_OUT_X_L_G;
}

/**
 * @return reg value that matches the given engineering value passed in.
 * The round_up flag is used to specify whether to round up or down.
 * Note, this function always returns a valid reg value. If the request is
 * outside the range of values, it returns the closest valid reg value.
 */
static int get_reg_val(const int eng_val, const int round_up,
		       const struct accel_param_pair *pairs, const int size)
{
	int i;
	for (i = 0; i < size - 1; i++) {
		if (eng_val <= pairs[i].val)
			break;

		if (eng_val < pairs[i + 1].val) {
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
			       const struct accel_param_pair *pairs,
			       const int size)
{
	int i;
	for (i = 0; i < size; i++) {
		if (reg_val == pairs[i].reg_val)
			break;
	}
	return pairs[i].val;
}

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
	int ret, ctrl_val, range_tbl_size;
	uint8_t ctrl_reg, reg_val;
	const struct accel_param_pair *ranges;

	ctrl_reg = get_ctrl_reg(s->type);
	ranges = get_range_table(s->type, &range_tbl_size);

	reg_val = get_reg_val(range, rnd, ranges, range_tbl_size);

	/*
	 * Lock accel resource to prevent another task from attempting
	 * to write accel parameters until we are done.
	 */
	mutex_lock(s->mutex);

	ret = raw_read8(s->port, s->i2c_spi_addr_flags, ctrl_reg, &ctrl_val);
	if (ret != EC_SUCCESS)
		goto accel_cleanup;

	ctrl_val = (ctrl_val & ~LSM6DS0_RANGE_MASK) | reg_val;
	ret = raw_write8(s->port, s->i2c_spi_addr_flags, ctrl_reg, ctrl_val);

	/* Now that we have set the range, update the driver's value. */
	if (ret == EC_SUCCESS)
		s->current_range =
			get_engineering_val(reg_val, ranges, range_tbl_size);

accel_cleanup:
	mutex_unlock(s->mutex);
	return ret;
}

static int get_resolution(const struct motion_sensor_t *s)
{
	return LSM6DS0_RESOLUTION;
}

static int set_data_rate(const struct motion_sensor_t *s, int rate, int rnd)
{
	int ret, val, odr_tbl_size;
	uint8_t ctrl_reg, reg_val;
	const struct accel_param_pair *data_rates;
	struct lsm6ds0_data *data = s->drv_data;

	ctrl_reg = get_ctrl_reg(s->type);
	data_rates = get_odr_table(s->type, &odr_tbl_size);
	reg_val = get_reg_val(rate, rnd, data_rates, odr_tbl_size);

	/*
	 * Lock accel resource to prevent another task from attempting
	 * to write accel parameters until we are done.
	 */
	mutex_lock(s->mutex);

	ret = raw_read8(s->port, s->i2c_spi_addr_flags, ctrl_reg, &val);
	if (ret != EC_SUCCESS)
		goto accel_cleanup;

	val = (val & ~LSM6DS0_ODR_MASK) | reg_val;
	ret = raw_write8(s->port, s->i2c_spi_addr_flags, ctrl_reg, val);

	/* Now that we have set the odr, update the driver's value. */
	if (ret == EC_SUCCESS)
		data->base.odr =
			get_engineering_val(reg_val, data_rates, odr_tbl_size);

	/* CTRL_REG3_G 12h
	 * [7] low-power mode = 0;
	 * [6] high pass filter disabled;
	 * [5:4] 0 keep const 0
	 * [3:0] HPCF_G
	 *       Table 48 Gyroscope high-pass filter cutoff frequency
	 */
	if (MOTIONSENSE_TYPE_GYRO == s->type) {
		ret = raw_read8(s->port, s->i2c_spi_addr_flags,
				LSM6DS0_CTRL_REG3_G, &val);
		if (ret != EC_SUCCESS)
			goto accel_cleanup;
		val &= ~(0x3 << 4); /* clear bit [5:4] */
		val = (rate > 119000) ?
			      (val | (1 << 7)) /* set high-power mode */ :
			      (val & ~(1 << 7)); /* set low-power mode */
		ret = raw_write8(s->port, s->i2c_spi_addr_flags,
				 LSM6DS0_CTRL_REG3_G, val);
	}

accel_cleanup:
	mutex_unlock(s->mutex);
	return ret;
}

static int get_data_rate(const struct motion_sensor_t *s)
{
	struct lsm6ds0_data *data = s->drv_data;

	return data->base.odr;
}

static int set_offset(const struct motion_sensor_t *s, const int16_t *offset,
		      int16_t temp)
{
	/* temperature is ignored */
	struct lsm6ds0_data *data = s->drv_data;
	data->offset[X] = offset[X];
	data->offset[Y] = offset[Y];
	data->offset[Z] = offset[Z];
	return EC_SUCCESS;
}

static int get_offset(const struct motion_sensor_t *s, int16_t *offset,
		      int16_t *temp)
{
	struct lsm6ds0_data *data = s->drv_data;
	offset[X] = data->offset[X];
	offset[Y] = data->offset[Y];
	offset[Z] = data->offset[Z];
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static int is_data_ready(const struct motion_sensor_t *s, int *ready)
{
	int ret, tmp;

	ret = raw_read8(s->port, s->i2c_spi_addr_flags, LSM6DS0_STATUS_REG,
			&tmp);

	if (ret != EC_SUCCESS) {
		CPRINTS("%s type:0x%X RS Error", s->name, s->type);
		return ret;
	}

	if (MOTIONSENSE_TYPE_ACCEL == s->type)
		*ready = (LSM6DS0_STS_XLDA_UP == (tmp & LSM6DS0_STS_XLDA_MASK));
	else
		*ready = (LSM6DS0_STS_GDA_UP == (tmp & LSM6DS0_STS_GDA_MASK));

	return EC_SUCCESS;
}

static int read(const struct motion_sensor_t *s, intv3_t v)
{
	uint8_t raw[6];
	uint8_t xyz_reg;
	int ret, i, tmp = 0;
	struct lsm6ds0_data *data = s->drv_data;

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
	ret = i2c_read_block(s->port, s->i2c_spi_addr_flags, xyz_reg, raw, 6);

	if (ret != EC_SUCCESS) {
		CPRINTS("%s type:0x%X RD XYZ Error", s->name, s->type);
		return ret;
	}

	for (i = X; i <= Z; i++)
		v[i] = (int16_t)((raw[i * 2 + 1] << 8) | raw[i * 2]);

	rotate(v, *s->rot_standard_ref, v);

	/* apply offset in the device coordinates */
	for (i = X; i <= Z; i++)
		v[i] += (data->offset[i] << 5) / s->current_range;

	return EC_SUCCESS;
}

static int init(struct motion_sensor_t *s)
{
	int ret = 0, tmp;

	ret = raw_read8(s->port, s->i2c_spi_addr_flags, LSM6DS0_WHO_AM_I_REG,
			&tmp);
	if (ret)
		return EC_ERROR_UNKNOWN;

	if (tmp != LSM6DS0_WHO_AM_I)
		return EC_ERROR_ACCESS_DENIED;

	/*
	 * This sensor can be powered through an EC reboot, so the state of
	 * the sensor is unknown here. Initiate software reset to restore
	 * sensor to default.
	 * [6] BDU Enable Block Data Update.
	 * [0] SW_RESET software reset
	 *
	 * lsm6ds0 supports both accel & gyro features
	 * Board will see two virtual sensor devices: accel & gyro.
	 * Requirement: Accel need be init before gyro.
	 * SW_RESET is down for accel only!
	 */
	if (MOTIONSENSE_TYPE_ACCEL == s->type) {
		mutex_lock(s->mutex);
		ret = raw_read8(s->port, s->i2c_spi_addr_flags,
				LSM6DS0_CTRL_REG8, &tmp);
		if (ret) {
			mutex_unlock(s->mutex);
			return EC_ERROR_UNKNOWN;
		}
		tmp |= (1 | LSM6DS0_BDU_ENABLE);
		ret = raw_write8(s->port, s->i2c_spi_addr_flags,
				 LSM6DS0_CTRL_REG8, tmp);
		mutex_unlock(s->mutex);

		if (ret)
			return ret;

		/* Power Down Gyro */
		ret = raw_write8(s->port, s->i2c_spi_addr_flags,
				 LSM6DS0_CTRL_REG1_G, 0x0);
		if (ret)
			return ret;
	}
	return sensor_init_done(s);
}

const struct accelgyro_drv lsm6ds0_drv = {
	.init = init,
	.read = read,
	.set_range = set_range,
	.get_resolution = get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = get_data_rate,
	.set_offset = set_offset,
	.get_offset = get_offset,
};
