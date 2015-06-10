/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * BMI160/BMC50 accelerometer and gyro module for Chrome EC
 * 3D digital accelerometer & 3D digital gyroscope
 */

#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/mag_bmm150.h"
#include "hooks.h"
#include "i2c.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ## args)

/*
 * Struct for pairing an engineering value with the register value for a
 * parameter.
 */
struct accel_param_pair {
	int val; /* Value in engineering units. */
	int reg_val; /* Corresponding register value. */
};

/* List of range values in +/-G's and their associated register values. */
static const struct accel_param_pair g_ranges[] = {
	{2, BMI160_GSEL_2G},
	{4, BMI160_GSEL_4G},
	{8, BMI160_GSEL_8G},
	{16, BMI160_GSEL_16G}
};

/*
 * List of angular rate range values in +/-dps's
 * and their associated register values.
 */
const struct accel_param_pair dps_ranges[] = {
	{125, BMI160_DPS_SEL_125},
	{250, BMI160_DPS_SEL_250},
	{500, BMI160_DPS_SEL_500},
	{1000, BMI160_DPS_SEL_1000},
	{2000, BMI160_DPS_SEL_2000}
};

static inline const struct accel_param_pair *get_range_table(
		enum motionsensor_type type, int *psize)
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

static inline int get_xyz_reg(enum motionsensor_type type)
{
	switch (type) {
	case MOTIONSENSE_TYPE_ACCEL:
		return BMI160_ACC_X_L_G;
	case MOTIONSENSE_TYPE_GYRO:
		return BMI160_GYR_X_L_G;
	case MOTIONSENSE_TYPE_MAG:
		return BMI160_MAG_X_L_G;
	default:
		return -1;
	}
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
		const struct accel_param_pair *pairs, const int size)
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
static inline int raw_read8(const int addr, const int reg, int *data_ptr)
{
	return i2c_read8(I2C_PORT_ACCEL, addr, reg, data_ptr);
}

/**
 * Write register from accelerometer.
 */
static inline int raw_write8(const int addr, const int reg, int data)
{
	return i2c_write8(I2C_PORT_ACCEL, addr, reg, data);
}

#ifdef CONFIG_MAG_BMI160_BMM150
/**
 * Control access to the compass on the secondary i2c interface:
 * enable values are:
 * 1: manual access, we can issue i2c to the compass
 * 0: data access: BMI160 gather data periodically from the compass.
 */
static int bmm150_mag_access_ctrl(const int addr, const int enable)
{
	int mag_if_ctrl;
	raw_read8(addr, BMI160_MAG_IF_1, &mag_if_ctrl);
	if (enable) {
		mag_if_ctrl |= BMI160_MAG_MANUAL_EN;
		mag_if_ctrl &= ~BMI160_MAG_READ_BURST_MASK;
		mag_if_ctrl |= BMI160_MAG_READ_BURST_1;
	} else {
		mag_if_ctrl &= ~BMI160_MAG_MANUAL_EN;
		mag_if_ctrl &= ~BMI160_MAG_READ_BURST_MASK;
		mag_if_ctrl |= BMI160_MAG_READ_BURST_8;
	}
	return raw_write8(addr, BMI160_MAG_IF_1, mag_if_ctrl);
}

/**
 * Read register from compass.
 * Assuming we are in manual access mode, read compass i2c register.
 */
static int raw_mag_read8(const int addr, const int reg, int *data_ptr)
{
	/* Only read 1 bytes */
	raw_write8(addr, BMI160_MAG_I2C_READ_ADDR, reg);
	return raw_read8(addr, BMI160_MAG_I2C_READ_DATA, data_ptr);
}

/**
 * Write register from compass.
 * Assuming we are in manual access mode, write to compass i2c register.
 */
static int raw_mag_write8(const int addr, const int reg, int data)
{
	raw_write8(addr, BMI160_MAG_I2C_WRITE_DATA, data);
	return raw_write8(addr, BMI160_MAG_I2C_WRITE_ADDR, reg);
}
#endif

static int set_range(const struct motion_sensor_t *s,
				int range,
				int rnd)
{
	int ret, range_tbl_size;
	uint8_t reg_val, ctrl_reg;
	const struct accel_param_pair *ranges;
	struct motion_data_t *data =
		&((struct bmi160_drv_data_t *)s->drv_data)->saved_data[s->type];

	if (s->type == MOTIONSENSE_TYPE_MAG) {
		data->range = range;
		return EC_SUCCESS;
	}

	ctrl_reg = BMI160_RANGE_REG(s->type);
	ranges = get_range_table(s->type, &range_tbl_size);
	reg_val = get_reg_val(range, rnd, ranges, range_tbl_size);

	ret = raw_write8(s->i2c_addr, ctrl_reg, reg_val);
	/* Now that we have set the range, update the driver's value. */
	if (ret == EC_SUCCESS)
		data->range = get_engineering_val(reg_val, ranges,
				range_tbl_size);
	return ret;
}

static int get_range(const struct motion_sensor_t *s,
				int *range)
{
	struct motion_data_t *data =
		&((struct bmi160_drv_data_t *)s->drv_data)->saved_data[s->type];

	*range = data->range;
	return EC_SUCCESS;
}

static int set_resolution(const struct motion_sensor_t *s,
				int res,
				int rnd)
{
	/* Only one resolution, BMI160_RESOLUTION, so nothing to do. */
	return EC_SUCCESS;
}

static int get_resolution(const struct motion_sensor_t *s,
				int *res)
{
	*res = BMI160_RESOLUTION;
	return EC_SUCCESS;
}

static int set_data_rate(const struct motion_sensor_t *s,
				int rate,
				int rnd)
{
	int ret, val, normalized_rate;
	uint8_t ctrl_reg, reg_val;
	struct motion_data_t *data =
		&((struct bmi160_drv_data_t *)s->drv_data)->saved_data[s->type];

	if (rate == 0) {
		/* suspend */
		ret = raw_write8(s->i2c_addr, BMI160_CMD_REG,
				 BMI160_CMD_MODE_SUSPEND(s->type));
		msleep(30);
		return ret;
	}
	ctrl_reg = BMI160_CONF_REG(s->type);
	reg_val = BMI160_ODR_TO_REG(rate);
	normalized_rate = BMI160_REG_TO_ODR(reg_val);
	if (rnd && (normalized_rate < rate)) {
		reg_val++;
		normalized_rate *= 2;
	}

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		if (reg_val > BMI160_ODR_1600HZ) {
			reg_val = BMI160_ODR_1600HZ;
			normalized_rate = 1600000;
		} else if (reg_val < BMI160_ODR_0_78HZ) {
			reg_val = BMI160_ODR_0_78HZ;
			normalized_rate = 780;
		}
		break;
	case MOTIONSENSE_TYPE_GYRO:
		if (reg_val > BMI160_ODR_3200HZ) {
			reg_val = BMI160_ODR_3200HZ;
			normalized_rate = 3200000;
		} else if (reg_val < BMI160_ODR_25HZ) {
			reg_val = BMI160_ODR_25HZ;
			normalized_rate = 25000;
		}
		break;
	case MOTIONSENSE_TYPE_MAG:
		if (reg_val > BMI160_ODR_800HZ) {
			reg_val = BMI160_ODR_800HZ;
			normalized_rate = 800000;
		} else if (reg_val < BMI160_ODR_0_78HZ) {
			reg_val = BMI160_ODR_0_78HZ;
			normalized_rate = 780;
		}
		break;

	default:
		return -1;
	}

	/*
	 * Lock accel resource to prevent another task from attempting
	 * to write accel parameters until we are done.
	 */
	mutex_lock(s->mutex);

	ret = raw_read8(s->i2c_addr, ctrl_reg, &val);
	if (ret != EC_SUCCESS)
		goto accel_cleanup;

	val = (val & ~BMI160_ODR_MASK) | reg_val;
	ret = raw_write8(s->i2c_addr, ctrl_reg, val);

	/* Now that we have set the odr, update the driver's value. */
	if (ret == EC_SUCCESS)
		data->odr = normalized_rate;

accel_cleanup:
	mutex_unlock(s->mutex);
	return ret;
}

static int get_data_rate(const struct motion_sensor_t *s,
				int *rate)
{
	struct motion_data_t *data =
		&((struct bmi160_drv_data_t *)s->drv_data)->saved_data[s->type];

	*rate = data->odr;
	return EC_SUCCESS;
}

void normalize(const struct motion_sensor_t *s, vector_3_t v, uint8_t *data)
{
	int range;

	v[0] = ((int16_t)((data[1] << 8) | data[0]));
	v[1] = ((int16_t)((data[3] << 8) | data[2]));
	v[2] = ((int16_t)((data[5] << 8) | data[4]));

	get_range(s, &range);

	v[0] *= range;
	v[1] *= range;
	v[2] *= range;

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		/* normalize the accel scale: 1G = 1024 */
		v[0] >>= 5;
		v[1] >>= 5;
		v[2] >>= 5;
		break;
	case MOTIONSENSE_TYPE_GYRO:
		v[0] >>= 8;
		v[1] >>= 8;
		v[2] >>= 8;
		break;
	default:
		break;
	}
}

#ifdef CONFIG_ACCEL_INTERRUPTS
static int set_interrupt(const struct motion_sensor_t *s,
			       unsigned int threshold)
{
	/* Currently unsupported. */
	return EC_ERROR_UNKNOWN;
}
#endif

static int is_data_ready(const struct motion_sensor_t *s, int *ready)
{
	int ret, tmp;

	ret = raw_read8(s->i2c_addr, BMI160_STATUS, &tmp);

	if (ret != EC_SUCCESS) {
		CPRINTF("[%T %s type:0x%X RS Error]", s->name, s->type);
		return ret;
	}

	*ready = tmp & BMI160_DRDY_MASK(s->type);
	return EC_SUCCESS;
}

static int read(const struct motion_sensor_t *s, vector_3_t v)
{
	uint8_t data[6];
	uint8_t xyz_reg;
	int ret, tmp = 0;

	ret = is_data_ready(s, &tmp);
	if (ret != EC_SUCCESS)
		return ret;

	/*
	 * If sensor data is not ready, return the previous read data.
	 * Note: return success so that motion senor task can read again
	 * to get the latest updated sensor data quickly.
	 */
	if (!tmp) {
		v[0] = s->raw_xyz[0];
		v[1] = s->raw_xyz[1];
		v[2] = s->raw_xyz[2];
		return EC_SUCCESS;
	}

	xyz_reg = get_xyz_reg(s->type);

	/* Read 6 bytes starting at xyz_reg */
	i2c_lock(I2C_PORT_ACCEL, 1);
	ret = i2c_xfer(I2C_PORT_ACCEL, s->i2c_addr,
			&xyz_reg, 1, data, 6, I2C_XFER_SINGLE);
	i2c_lock(I2C_PORT_ACCEL, 0);

	if (ret != EC_SUCCESS) {
		CPRINTF("[%T %s type:0x%X RD XYZ Error %d]",
			s->name, s->type, ret);
		return ret;
	}
	normalize(s, v, data);
	return EC_SUCCESS;
}

static int init(const struct motion_sensor_t *s)
{
	int ret = 0, tmp;

	ret = raw_read8(s->i2c_addr, BMI160_CHIP_ID, &tmp);
	if (ret)
		return EC_ERROR_UNKNOWN;

	if (tmp != BMI160_CHIP_ID_MAJOR)
		return EC_ERROR_ACCESS_DENIED;


	if (s->type == MOTIONSENSE_TYPE_ACCEL) {
		struct bmi160_drv_data_t *data =
			(struct bmi160_drv_data_t *)s->drv_data;

		/* Reset the chip to be in a good state */
		raw_write8(s->i2c_addr, BMI160_CMD_REG,
				BMI160_CMD_SOFT_RESET);
		msleep(30);
		data->flags &= ~BMI160_FLAG_SEC_I2C_ENABLED;
		/* To avoid gyro wakeup */
		raw_write8(s->i2c_addr, BMI160_PMU_TRIGGER, 0);
	}

	raw_write8(s->i2c_addr, BMI160_CMD_REG,
			BMI160_CMD_MODE_NORMAL(s->type));
	msleep(30);

	set_range(s, s->runtime_config.range, 0);
	set_data_rate(s, s->runtime_config.odr, 0);

#ifdef CONFIG_MAG_BMI160_BMM150
	if (s->type == MOTIONSENSE_TYPE_MAG) {
		struct bmi160_drv_data_t *data =
			(struct bmi160_drv_data_t *)s->drv_data;
		if ((data->flags & BMI160_FLAG_SEC_I2C_ENABLED) == 0) {
			int ext_page_reg, pullup_reg;
			/* Enable secondary interface */
			/*
			 * This is not part of the normal configuration but from
			 * code on Bosh github repo:
			 * https://github.com/BoschSensortec/BMI160_driver
			 *
			 * Magic command sequences
			 */
			raw_write8(s->i2c_addr, BMI160_CMD_REG,
					BMI160_CMD_EXT_MODE_EN_B0);
			raw_write8(s->i2c_addr, BMI160_CMD_REG,
					BMI160_CMD_EXT_MODE_EN_B1);
			raw_write8(s->i2c_addr, BMI160_CMD_REG,
					BMI160_CMD_EXT_MODE_EN_B2);

			/*
			 * Change the register page to target mode, to change
			 * the internal pull ups of the secondary interface.
			 */
			raw_read8(s->i2c_addr, BMI160_CMD_EXT_MODE_ADDR,
					&ext_page_reg);
			raw_write8(s->i2c_addr, BMI160_CMD_EXT_MODE_ADDR,
					ext_page_reg | BMI160_CMD_TARGET_PAGE);
			raw_read8(s->i2c_addr, BMI160_CMD_EXT_MODE_ADDR,
					&ext_page_reg);
			raw_write8(s->i2c_addr, BMI160_CMD_EXT_MODE_ADDR,
					ext_page_reg | BMI160_CMD_PAGING_EN);
			raw_read8(s->i2c_addr, BMI160_COM_C_TRIM_ADDR,
					&pullup_reg);
			raw_write8(s->i2c_addr, BMI160_COM_C_TRIM_ADDR,
					pullup_reg | BMI160_COM_C_TRIM);
			raw_read8(s->i2c_addr, BMI160_CMD_EXT_MODE_ADDR,
					&ext_page_reg);
			raw_write8(s->i2c_addr, BMI160_CMD_EXT_MODE_ADDR,
					ext_page_reg & ~BMI160_CMD_TARGET_PAGE);
			raw_read8(s->i2c_addr, BMI160_CMD_EXT_MODE_ADDR,
					&ext_page_reg);

			/* Set the i2c address of the compass */
			ret = raw_write8(s->i2c_addr, BMI160_MAG_IF_0,
					BMM150_I2C_ADDRESS);

			/* Enable the secondary interface as I2C */
			ret = raw_write8(s->i2c_addr, BMI160_IF_CONF,
				BMI160_IF_MODE_AUTO_I2C << BMI160_IF_MODE_OFF);
			data->flags |= BMI160_FLAG_SEC_I2C_ENABLED;
		}


		bmm150_mag_access_ctrl(s->i2c_addr, 1);
		/* Set the compass from Suspend to Sleep */
		ret = raw_mag_write8(s->i2c_addr, BMM150_PWR_CTRL,
				BMM150_PWR_ON);
		/* Now we can read the device id */
		ret = raw_mag_read8(s->i2c_addr, BMM150_CHIP_ID, &tmp);
		if (ret)
			return EC_ERROR_UNKNOWN;

		if (tmp != BMM150_CHIP_ID_MAJOR)
			return EC_ERROR_ACCESS_DENIED;

		/* Leave the address for reading the data */
		raw_write8(s->i2c_addr, BMI160_MAG_I2C_READ_ADDR,
				BMM150_BASE_DATA);
		/*
		 * Set the compass forced mode, to sleep after each measure.
		 */
		ret = raw_mag_write8(s->i2c_addr, BMM150_OP_CTRL,
			BMM150_OP_MODE_FORCED << BMM150_OP_MODE_OFFSET);

		/*
		 * Put back the secondary interface in normal mode.
		 * BMI160 will poll based on the configure ODR.
		 */
		bmm150_mag_access_ctrl(s->i2c_addr, 0);
	}
#endif

	/* Fifo setup is done elsewhere */
	CPRINTF("[%T %s: MS Done Init type:0x%X range:%d odr:%d]\n",
			s->name, s->type, s->runtime_config.range,
			s->runtime_config.odr);
	return ret;
}

const struct accelgyro_drv bmi160_drv = {
	.init = init,
	.read = read,
	.set_range = set_range,
	.get_range = get_range,
	.set_resolution = set_resolution,
	.get_resolution = get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = get_data_rate,
#ifdef CONFIG_ACCEL_INTERRUPTS
	.set_interrupt = set_interrupt,
#endif
};
