/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Kionix Accelerometer driver for Chrome EC
 *
 * Supported: KX022, KXCJ9
 */

#include "accel_kionix.h"
#include "accel_kx022.h"
#include "accel_kxcj9.h"
#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "i2c.h"
#include "math_util.h"
#include "motion_orientation.h"
#include "spi.h"
#include "task.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ##args)

/* Number of times to attempt to enable sensor before giving up. */
#define SENSOR_ENABLE_ATTEMPTS 3

#if !defined(CONFIG_ACCEL_KXCJ9) && !defined(CONFIG_ACCEL_KX022)
#error "Must use either KXCJ9 or KX022"
#endif

#if defined(CONFIG_ACCEL_KXCJ9) && !defined(CONFIG_ACCEL_KX022)
#define V(s_) 1
#elif defined(CONFIG_ACCEL_KX022) && !defined(CONFIG_ACCEL_KXCJ9)
#define V(s_) 0
#else
#define V(s_) ((s_)->chip == MOTIONSENSE_CHIP_KXCJ9)
#endif
/* Index for which table to use. */
#if !defined(CONFIG_ACCEL_KXCJ9) || !defined(CONFIG_ACCEL_KX022)
#define T(s_) 0
#else
#define T(s_) V(s_)
#endif /* !defined(CONFIG_ACCEL_KXCJ9) || !defined(CONFIG_ACCEL_KX022) */

STATIC_IF(CONFIG_KX022_ORIENTATION_SENSOR)
int check_orientation_locked(const struct motion_sensor_t *s);

/* List of range values in +/-G's and their associated register values. */
static const struct accel_param_pair ranges[][3] = {
#ifdef CONFIG_ACCEL_KX022
	{ { 2, KX022_GSEL_2G }, { 4, KX022_GSEL_4G }, { 8, KX022_GSEL_8G } },
#endif /* defined(CONFIG_ACCEL_KX022) */
#ifdef CONFIG_ACCEL_KXCJ9
	{ { 2, KXCJ9_GSEL_2G },
	  { 4, KXCJ9_GSEL_4G },
	  { 8, KXCJ9_GSEL_8G_14BIT } },
#endif /* defined(CONFIG_ACCEL_KXCJ9) */
};

/* List of resolution values in bits and their associated register values. */
static const struct accel_param_pair resolutions[][2] = {
#ifdef CONFIG_ACCEL_KX022
	{ { 8, KX022_RES_8BIT }, { 16, KX022_RES_16BIT } },
#endif /* defined(CONFIG_ACCEL_KX022) */
#ifdef CONFIG_ACCEL_KXCJ9
	{ { 8, KXCJ9_RES_8BIT }, { 12, KXCJ9_RES_12BIT } },
#endif /* defined(CONFIG_ACCEL_KXCJ9) */
};

/* List of ODR values in mHz and their associated register values. */
static const struct accel_param_pair datarates[][13] = {
#ifdef CONFIG_ACCEL_KX022
	/* One duplicate because table sizes must match. */
	{ { 781, KX022_OSA_0_781HZ },
	  { 781, KX022_OSA_0_781HZ },
	  { 1563, KX022_OSA_1_563HZ },
	  { 3125, KX022_OSA_3_125HZ },
	  { 6250, KX022_OSA_6_250HZ },
	  { 12500, KX022_OSA_12_50HZ },
	  { 25000, KX022_OSA_25_00HZ },
	  { 50000, KX022_OSA_50_00HZ },
	  { 100000, KX022_OSA_100_0HZ },
	  { 200000, KX022_OSA_200_0HZ },
	  { 400000, KX022_OSA_400_0HZ },
	  { 800000, KX022_OSA_800_0HZ },
	  { 1600000, KX022_OSA_1600HZ } },
#endif /* defined(CONFIG_ACCEL_KX022) */
#ifdef CONFIG_ACCEL_KXCJ9
	{ { 0, KXCJ9_OSA_0_000HZ },
	  { 781, KXCJ9_OSA_0_781HZ },
	  { 1563, KXCJ9_OSA_1_563HZ },
	  { 3125, KXCJ9_OSA_3_125HZ },
	  { 6250, KXCJ9_OSA_6_250HZ },
	  { 12500, KXCJ9_OSA_12_50HZ },
	  { 25000, KXCJ9_OSA_25_00HZ },
	  { 50000, KXCJ9_OSA_50_00HZ },
	  { 100000, KXCJ9_OSA_100_0HZ },
	  { 200000, KXCJ9_OSA_200_0HZ },
	  { 400000, KXCJ9_OSA_400_0HZ },
	  { 800000, KXCJ9_OSA_800_0HZ },
	  { 1600000, KXCJ9_OSA_1600_HZ } },
#endif /* defined(CONFIG_ACCEL_KXCJ9) */
};

/**
 * Find index into a accel_param_pair that matches the given engineering value
 * passed in. The round_up flag is used to specify whether to round up or down.
 * Note, this function always returns a valid index. If the request is
 * outside the range of values, it returns the closest valid index.
 */
static int find_param_index(const int eng_val, const int round_up,
			    const struct accel_param_pair *pairs,
			    const int size)
{
	int i;

	/* Linear search for index to match. */
	for (i = 0; i < size - 1; i++) {
		if (eng_val <= pairs[i].val)
			return i;

		if (eng_val < pairs[i + 1].val) {
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
static int raw_read8(const int port, const uint16_t i2c_spi_addr_flags,
		     const int reg, int *data_ptr)
{
	int rv = EC_ERROR_INVAL;

	if (ACCEL_ADDR_IS_SPI(i2c_spi_addr_flags)) {
#ifdef CONFIG_SPI_ACCEL_PORT
		uint8_t val;
		uint8_t cmd = 0x80 | reg;

		rv = spi_transaction(
			&spi_devices[ACCEL_GET_SPI_ADDR(i2c_spi_addr_flags)],
			&cmd, 1, &val, 1);
		if (rv == EC_SUCCESS)
			*data_ptr = val;

#endif
	} else {
		rv = i2c_read8(port, i2c_spi_addr_flags, reg, data_ptr);
	}
	return rv;
}

/**
 * Write register from accelerometer.
 */
static int raw_write8(const int port, const uint16_t i2c_spi_addr_flags,
		      const int reg, int data)
{
	int rv = EC_ERROR_INVAL;

	if (ACCEL_ADDR_IS_SPI(i2c_spi_addr_flags)) {
#ifdef CONFIG_SPI_ACCEL_PORT
		uint8_t cmd[2] = { reg, data };

		rv = spi_transaction(
			&spi_devices[ACCEL_GET_SPI_ADDR(i2c_spi_addr_flags)],
			cmd, 2, NULL, 0);
#endif
	} else {
		rv = i2c_write8(port, i2c_spi_addr_flags, reg, data);
	}
	return rv;
}

static int raw_read_multi(const int port, const uint16_t i2c_spi_addr_flags,
			  uint8_t reg, uint8_t *rxdata, int rxlen)
{
	int rv = EC_ERROR_INVAL;

	if (ACCEL_ADDR_IS_SPI(i2c_spi_addr_flags)) {
#ifdef CONFIG_SPI_ACCEL_PORT
		reg |= 0x80;
		rv = spi_transaction(
			&spi_devices[ACCEL_GET_SPI_ADDR(i2c_spi_addr_flags)],
			&reg, 1, rxdata, rxlen);
#endif
	} else {
		rv = i2c_read_block(port, i2c_spi_addr_flags, reg, rxdata,
				    rxlen);
	}
	return rv;
}

/**
 * Disable sensor by taking it out of operating mode. When disabled, the
 * acceleration data does not change.
 *
 * Note: This is intended to be called in a pair with enable_sensor().
 *
 * @param s Pointer to motion sensor data
 * @param reg_val Pointer to location to store control register after disabling
 *
 * @return EC_SUCCESS if successful, EC_ERROR_* otherwise
 */
static int disable_sensor(const struct motion_sensor_t *s, int *reg_val)
{
	int i, ret, reg, pc1_field;

	reg = KIONIX_CTRL1_REG(V(s));
	pc1_field = KIONIX_PC1_FIELD(V(s));

	/*
	 * Read the current state of the control register
	 * so that we can restore it later.
	 */
	for (i = 0; i < SENSOR_ENABLE_ATTEMPTS; i++) {
		ret = raw_read8(s->port, s->i2c_spi_addr_flags, reg, reg_val);
		if (ret != EC_SUCCESS)
			continue;

		*reg_val &= ~pc1_field;

		ret = raw_write8(s->port, s->i2c_spi_addr_flags, reg, *reg_val);
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
 * @param s Pointer to motion sensor data
 * @param reg_val Value of the control register to write to sensor
 *
 * @return EC_SUCCESS if successful, EC_ERROR_* otherwise
 */
static int enable_sensor(const struct motion_sensor_t *s, int reg_val)
{
	int i, ret, reg, pc1_field;

	reg = KIONIX_CTRL1_REG(V(s));
	pc1_field = KIONIX_PC1_FIELD(V(s));

	for (i = 0; i < SENSOR_ENABLE_ATTEMPTS; i++) {
		ret = raw_read8(s->port, s->i2c_spi_addr_flags, reg, &reg_val);
		if (ret != EC_SUCCESS)
			continue;

		/* Enable tilt orientation mode  if lid sensor */
		if (IS_ENABLED(CONFIG_KX022_ORIENTATION_SENSOR) &&
		    (s->location == MOTIONSENSE_LOC_LID) && (V(s) == 0))
			reg_val |= KX022_CNTL1_TPE;

		/* Enable accelerometer based on reg_val value. */
		ret = raw_write8(s->port, s->i2c_spi_addr_flags, reg,
				 reg_val | pc1_field);

		/* On first success, we are done. */
		if (ret == EC_SUCCESS)
			break;
	}
	return ret;
}

/**
 * Set a register value.
 *
 * @param s Pointer to motion sensor data
 * @param reg     Register to write to
 * @param reg_val Value of the control register to write to sensor
 * @param field   Bitfield to modify.
 *
 * @return EC_SUCCESS if successful, EC_ERROR_* otherwise
 */
static int set_value(const struct motion_sensor_t *s, int reg, int val,
		     int field)
{
	int ret, reg_val_new, reg_val;

	/* Disable the sensor to allow for changing of critical parameters. */
	mutex_lock(s->mutex);
	ret = disable_sensor(s, &reg_val);
	if (ret != EC_SUCCESS) {
		mutex_unlock(s->mutex);
		return ret;
	}

	/* Determine new value of control reg and attempt to write it. */
	reg_val_new = (reg_val & ~field) | val;
	ret = raw_write8(s->port, s->i2c_spi_addr_flags, reg, reg_val_new);

	/* If successfully written, then save the range. */
	if (ret == EC_SUCCESS)
		/* Re-enable the sensor. */
		ret = enable_sensor(s, reg_val_new);

	mutex_unlock(s->mutex);
	return ret;
}

static int set_range(struct motion_sensor_t *s, int range, int rnd)
{
	int ret, index, reg, range_field, range_val;

	/* Find index for interface pair matching the specified range. */
	index = find_param_index(range, rnd, ranges[T(s)],
				 ARRAY_SIZE(ranges[T(s)]));
	range_field = KIONIX_RANGE_FIELD(V(s));
	reg = KIONIX_CTRL1_REG(V(s));
	range_val = ranges[T(s)][index].reg;

	ret = set_value(s, reg, range_val, range_field);
	if (ret == EC_SUCCESS)
		s->current_range = ranges[T(s)][index].val;
	return ret;
}

static int set_resolution(const struct motion_sensor_t *s, int res, int rnd)
{
	int ret, index, reg, res_field, res_val;
	struct kionix_accel_data *data = s->drv_data;

	/* Find index for interface pair matching the specified resolution. */
	index = find_param_index(res, rnd, resolutions[T(s)],
				 ARRAY_SIZE(resolutions[T(s)]));
	res_val = resolutions[T(s)][index].reg;
	res_field = KIONIX_RES_FIELD(V(s));
	reg = KIONIX_CTRL1_REG(V(s));

	ret = set_value(s, reg, res_val, res_field);
	if (ret == EC_SUCCESS)
		data->sensor_resolution = resolutions[T(s)][index].val;
	return ret;
}

static int get_resolution(const struct motion_sensor_t *s)
{
	struct kionix_accel_data *data = s->drv_data;

	return data->sensor_resolution;
}

static int set_data_rate(const struct motion_sensor_t *s, int rate, int rnd)
{
	int ret, index, reg, odr_field, odr_val;
	struct kionix_accel_data *data = s->drv_data;

	/* Find index for interface pair matching the specified rate. */
	index = find_param_index(rate, rnd, datarates[T(s)],
				 ARRAY_SIZE(datarates[T(s)]));
	odr_val = datarates[T(s)][index].reg;
	reg = KIONIX_ODR_REG(V(s));
	odr_field = KIONIX_ODR_FIELD(V(s));

	ret = set_value(s, reg, odr_val, odr_field);
	if (ret == EC_SUCCESS)
		data->base.odr = datarates[T(s)][index].val;
	return ret;
}

static int get_data_rate(const struct motion_sensor_t *s)
{
	struct kionix_accel_data *data = s->drv_data;

	return data->base.odr;
}

static int set_offset(const struct motion_sensor_t *s, const int16_t *offset,
		      int16_t temp)
{
	/* temperature is ignored */
	struct kionix_accel_data *data = s->drv_data;
	data->offset[X] = offset[X];
	data->offset[Y] = offset[Y];
	data->offset[Z] = offset[Z];
	return EC_SUCCESS;
}

static int get_offset(const struct motion_sensor_t *s, int16_t *offset,
		      int16_t *temp)
{
	struct kionix_accel_data *data = s->drv_data;
	offset[X] = data->offset[X];
	offset[Y] = data->offset[Y];
	offset[Z] = data->offset[Z];
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static __maybe_unused enum motionsensor_orientation
kx022_convert_orientation(const struct motion_sensor_t *s, int orientation)
{
	enum motionsensor_orientation res = MOTIONSENSE_ORIENTATION_UNKNOWN;

	switch (orientation) {
	case KX022_ORIENT_PORTRAIT:
		res = MOTIONSENSE_ORIENTATION_PORTRAIT;
		break;
	case KX022_ORIENT_INVERT_PORTRAIT:
		res = MOTIONSENSE_ORIENTATION_UPSIDE_DOWN_PORTRAIT;
		break;
	case KX022_ORIENT_LANDSCAPE:
		res = MOTIONSENSE_ORIENTATION_LANDSCAPE;
		break;
	case KX022_ORIENT_INVERT_LANDSCAPE:
		res = MOTIONSENSE_ORIENTATION_UPSIDE_DOWN_LANDSCAPE;
		break;
	default:
		break;
	}
	res = motion_orientation_remap(s, res);
	return res;
}

#ifdef CONFIG_KX022_ORIENTATION_SENSOR
static int check_orientation_locked(const struct motion_sensor_t *s)
{
	struct kionix_accel_data *data = s->drv_data;
	int orientation, raw_orientation;
	int ret;

	ret = raw_read8(s->port, s->i2c_spi_addr_flags, KX022_TSCP,
			&raw_orientation);
	if (ret != EC_SUCCESS)
		return ret;

	/* mask off up and down events, we don't care about those */
	raw_orientation &= KX022_ORIENT_MASK;
	if (raw_orientation && (raw_orientation != data->raw_orientation)) {
		data->raw_orientation = raw_orientation;
		orientation = kx022_convert_orientation(s, raw_orientation);
		*motion_orientation_ptr(s) = orientation;
	}
	return ret;
}

bool motion_orientation_changed(const struct motion_sensor_t *s)
{
	return ((struct kionix_accel_data *)s->drv_data)->orientation !=
	       ((struct kionix_accel_data *)s->drv_data)->last_orientation;
}

enum motionsensor_orientation *
motion_orientation_ptr(const struct motion_sensor_t *s)
{
	return &((struct kionix_accel_data *)s->drv_data)->orientation;
}

void motion_orientation_update(const struct motion_sensor_t *s)
{
	((struct kionix_accel_data *)s->drv_data)->last_orientation =
		((struct kionix_accel_data *)s->drv_data)->orientation;
}

#endif

static int read(const struct motion_sensor_t *s, intv3_t v)
{
	uint8_t acc[6];
	uint8_t reg;
	int ret, i, resolution;
	struct kionix_accel_data *data = s->drv_data;

	/* Read 6 bytes starting at XOUT_L. */
	reg = KIONIX_XOUT_L(V(s));
	mutex_lock(s->mutex);
	ret = raw_read_multi(s->port, s->i2c_spi_addr_flags, reg, acc, 6);
	if (IS_ENABLED(CONFIG_KX022_ORIENTATION_SENSOR) &&
	    (s->location == MOTIONSENSE_LOC_LID) && (V(s) == 0) &&
	    (ret == EC_SUCCESS))
		ret = check_orientation_locked(s);
	mutex_unlock(s->mutex);

	if (ret != EC_SUCCESS)
		return ret;

	/*
	 * Convert acceleration to a signed 16-bit number. Note, based on
	 * the order of the registers:
	 *
	 * acc[0] = XOUT_L
	 * acc[1] = XOUT_H
	 * acc[2] = YOUT_L
	 * acc[3] = YOUT_H
	 * acc[4] = ZOUT_L
	 * acc[5] = ZOUT_H
	 *
	 * Add calibration offset before returning the data.
	 */
	resolution = get_resolution(s);
	for (i = X; i <= Z; i++) {
		if (V(s)) {
			v[i] = (((int8_t)acc[i * 2 + 1]) << 4) |
			       (acc[i * 2] >> 4);
			v[i] <<= 16 - resolution;
		} else {
			if (resolution == 8)
				acc[i * 2] = 0;
			v[i] = (((int8_t)acc[i * 2 + 1]) << 8) | acc[i * 2];
		}
	}
	rotate(v, *s->rot_standard_ref, v);

	/* apply offset in the device coordinates */
	for (i = X; i <= Z; i++)
		v[i] += (data->offset[i] << 5) / s->current_range;

	return EC_SUCCESS;
}

static int init(struct motion_sensor_t *s)
{
	int ret, val, reg, reset_field;
	uint8_t timeout;

	mutex_lock(s->mutex);
	if (V(s)) {
		/* The chip can take up to 10ms to boot */
		reg = KIONIX_WHO_AM_I(V(s));
		timeout = 0;
		do {
			crec_msleep(1);
			/* Read WHO_AM_I to be sure the device has booted */
			ret = raw_read8(s->port, s->i2c_spi_addr_flags, reg,
					&val);
			if (ret == EC_SUCCESS)
				break;

			/* Check for timeout. */
			if (timeout++ > 20) {
				ret = EC_ERROR_TIMEOUT;
				break;
			}
		} while (1);
	} else {
		/* Write 0x00 to the internal register for KX022 */
		reg = KX022_INTERNAL;
		ret = raw_write8(s->port, s->i2c_spi_addr_flags, reg, 0x0);
		if (ret != EC_SUCCESS) {
			/*
			 * For I2C communication, if ACK was not received
			 * from the first address, resend the command using
			 * the second address.
			 */
			if (!ACCEL_ADDR_IS_SPI(s->i2c_spi_addr_flags)) {
				const uint16_t i2c_alt_addr_flags =
					I2C_STRIP_FLAGS(s->i2c_spi_addr_flags) &
					~2;
				ret = raw_write8(s->port, i2c_alt_addr_flags,
						 reg, 0x0);
			}
		}
	}

	if (ret != EC_SUCCESS)
		goto reset_failed;

	/* Issue a software reset. */
	reg = KIONIX_CTRL2_REG(V(s));
	reset_field = KIONIX_RESET_FIELD(V(s));

	if (V(s)) {
		/* Place the sensor in standby mode to make changes. */
		ret = disable_sensor(s, &val);
		if (ret != EC_SUCCESS)
			goto reset_failed;
		ret = raw_read8(s->port, s->i2c_spi_addr_flags, reg, &val);
		if (ret != EC_SUCCESS)
			goto reset_failed;

		val |= reset_field;
	} else {
		/* Write 0 to CTRL2 for KX022 */
		ret = raw_write8(s->port, s->i2c_spi_addr_flags, reg, 0x0);
		if (ret != EC_SUCCESS)
			goto reset_failed;

		val = reset_field;
	}

	ret = raw_write8(s->port, s->i2c_spi_addr_flags, reg, val);
	if (ret != EC_SUCCESS)
		goto reset_failed;

	if (V(s)) {
		/* The SRST will be cleared when reset is complete. */
		timeout = 0;
		do {
			crec_msleep(1);

			ret = raw_read8(s->port, s->i2c_spi_addr_flags, reg,
					&val);
			/* Reset complete. */
			if ((ret == EC_SUCCESS) && !(val & reset_field))
				break;
			/* Check for timeout. */
			if (timeout++ > 20) {
				ret = EC_ERROR_TIMEOUT;
				goto reset_failed;
			}
		} while (1);
	} else {
		/* Wait 2 milliseconds for completion of the software reset. */
		crec_msleep(2);

		reg = KX022_COTR;
		ret = raw_read8(s->port, s->i2c_spi_addr_flags, reg, &val);
		if (val != KX022_COTR_VAL_DEFAULT) {
			CPRINTF("[%s: the software reset failed]\n", s->name);
			ret = EC_ERROR_HW_INTERNAL;
			goto reset_failed;
		}
	}

	reg = KIONIX_WHO_AM_I(V(s));
	ret = raw_read8(s->port, s->i2c_spi_addr_flags, reg, &val);
	if (ret != EC_SUCCESS || val != KIONIX_WHO_AM_I_VAL(V(s))) {
		ret = EC_ERROR_HW_INTERNAL;
		goto reset_failed;
	}

	mutex_unlock(s->mutex);

	/* Initialize with the desired parameters. */
	if (V(s))
		ret = set_resolution(s, 12, 1);
	else
		ret = set_resolution(s, 16, 1);
	if (ret != EC_SUCCESS)
		return ret;

	return sensor_init_done(s);

reset_failed:
	mutex_unlock(s->mutex);
	return ret;
}

static int probe(const struct motion_sensor_t *s)
{
	int val;

	if (i2c_read8(s->port, s->i2c_spi_addr_flags, KIONIX_WHO_AM_I(V(s)),
		      &val) != EC_SUCCESS)
		return EC_ERROR_HW_INTERNAL;

	if (val != KIONIX_WHO_AM_I_VAL(V(s)))
		return EC_ERROR_HW_INTERNAL;

	return EC_SUCCESS;
}

const struct accelgyro_drv kionix_accel_drv = {
	.init = init,
	.read = read,
	.set_range = set_range,
	.set_resolution = set_resolution,
	.get_resolution = get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = get_data_rate,
	.set_offset = set_offset,
	.get_offset = get_offset,
	.probe = probe,
};

#ifdef CONFIG_CMD_I2C_STRESS_TEST_ACCEL
struct i2c_stress_test_dev kionix_i2c_stress_test_dev = {
	.reg_info = {
		.read_reg = KIONIX_WHO_AM_I(V(s)),
		.read_val = KIONIX_WHO_AM_I_VAL(V(s)),
		.write_reg = KIONIX_ODR_REG(V(s)),
	},
	.i2c_read = &raw_read8,
	.i2c_write = &raw_write8,
};
#endif /* CONFIG_CMD_I2C_STRESS_TEST_ACCEL */
