/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Vishay VEML3328 light sensor driver
 */
#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "driver/als_veml3328.h"
#include "i2c.h"

#define CPRINTS(format, args...) cprints(CC_MOTION_SENSE, format, ##args)
#define VEML3328_MIN_LIGHT_THRES (10)
#define VEML3328_MAX_LIGHT_THRES (65535)

#define VEML3328_DRV_DATA(_s) ((struct als_drv_data_t *)(_s)->drv_data)
#define VEML3328_RGB_DRV_DATA(_s) \
	((struct veml3328_rgb_drv_data_t *)(_s)->drv_data)

/*
 * Read data from VEML3328 light sensor, and transfer unit into lux.
 */
static int veml3328_read(const struct motion_sensor_t *s, intv3_t v)
{
	struct als_drv_data_t *als_data = VEML3328_DRV_DATA(s);
	struct veml3328_rgb_drv_data_t *drv_data = VEML3328_RGB_DRV_DATA(s + 1);
	struct veml3328_calib *calib = &drv_data->calib;
	int addr = s->i2c_spi_addr_flags;
	int port = s->port;
	int raw;
	float lux;

	if (drv_data->calibration_mode) {
		RETURN_ERROR(i2c_read16(port, addr, VEML3328_REG_C, &raw));
		v[0] = raw;
	} else {
		RETURN_ERROR(i2c_read16(port, addr, VEML3328_REG_G, &raw));
		raw = MAX(raw, 1);
		lux = calib->LG * raw / VEML3328_DEFAULT_GAIN;
		v[0] = (int)lux;
	}

	v[1] = 0;
	v[2] = 0;

	/*
	 * Return an error when value didn't change to prevent filling the
	 * fifo with useless data.
	 */
	if (v[0] == als_data->last_value)
		return EC_ERROR_UNCHANGED;

	als_data->last_value = v[0];

	return EC_SUCCESS;
}

static int veml3328_set_range(struct motion_sensor_t *s, int range, int rnd)
{
	return EC_SUCCESS;
}

static int veml3328_set_data_rate(const struct motion_sensor_t *s, int rate,
				  int roundup)
{
	/* TODO(b/312586806): validate rate is valid */
	VEML3328_DRV_DATA(s)->rate = rate;
	return EC_SUCCESS;
}

static int veml3328_get_data_rate(const struct motion_sensor_t *s)
{
	return VEML3328_DRV_DATA(s)->rate;
}

static int veml3328_get_scale(const struct motion_sensor_t *s, uint16_t *scale,
			      int16_t *temp)
{
	scale[X] = VEML3328_DRV_DATA(s)->als_cal.channel_scale.k_channel_scale;
	scale[Y] = 0;
	scale[Z] = 0;
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static int veml3328_set_scale(const struct motion_sensor_t *s,
			      const uint16_t *scale, int16_t temp)
{
	if (scale[X] == 0)
		return EC_ERROR_INVAL;
	VEML3328_DRV_DATA(s)->als_cal.channel_scale.k_channel_scale = scale[X];
	return EC_SUCCESS;
}

static int veml3328_set_offset(const struct motion_sensor_t *s,
			       const int16_t *offset, int16_t temp)
{
	/* TODO(b/312586806): check calibration method */
	return EC_SUCCESS;
}

static int veml3328_get_offset(const struct motion_sensor_t *s, int16_t *offset,
			       int16_t *temp)
{
	offset[X] = VEML3328_DRV_DATA(s)->als_cal.offset;
	offset[Y] = 0;
	offset[Z] = 0;
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;

	return EC_SUCCESS;
}

static int veml3328_perform_calib(struct motion_sensor_t *s, int enable)
{
	VEML3328_RGB_DRV_DATA(s + 1)->calibration_mode = enable;
	return EC_SUCCESS;
}

/**
 * Initialise VEML3328 light sensor.
 */
static int veml3328_init(struct motion_sensor_t *s)
{
	int ret;
	int id;

	CPRINTS("veml3328 ALS init start");

	if (s->i2c_spi_addr_flags != VEML3328_I2C_ADDR) {
		CPRINTS("veml3328 address has to be %d", VEML3328_I2C_ADDR);
		return EC_ERROR_INVAL;
	}

	/* Shutdown */
	ret = i2c_write16(s->port, s->i2c_spi_addr_flags, VEML3328_REG_CONF,
			  VEML3328_SD);
	if (ret != EC_SUCCESS) {
		CPRINTS("veml3328 error writing to CONF reg %d", ret);
		return ret;
	}

	/* TODO(b/312586806) - what should be the reset timing ?? */
	crec_msleep(1);

	/* Power on, write default config */
	ret = i2c_write16(s->port, s->i2c_spi_addr_flags, VEML3328_REG_CONF,
			  VEML3328_CONF_DEFAULT);
	if (ret != EC_SUCCESS) {
		CPRINTS("veml3328 error writing to CONF reg %d", ret);
		return ret;
	}

	/* TODO(b/312586806) - what should be the reset timing ?? */
	crec_msleep(1);

	/* Check chip ID */
	ret = i2c_read16(s->port, s->i2c_spi_addr_flags, VEML3328_REG_ID, &id);
	if (ret != EC_SUCCESS) {
		CPRINTS("veml3328 failed reading ID reg ret=%d", ret);
		return ret;
	}

	id &= VEML3328_DEV_ID_MASK;
	if (id != VEML3328_DEV_ID) {
		CPRINTS("veml3328 wrong chip ID=%d", id);
		return EC_ERROR_INVAL;
	}

	CPRINTS("veml3328 ALS init successful");

	return sensor_init_done(s);
}

const struct accelgyro_drv veml3328_drv = {
	.init = veml3328_init,
	.read = veml3328_read,
	.set_range = veml3328_set_range,
	.set_offset = veml3328_set_offset,
	.get_offset = veml3328_get_offset,
	.set_scale = veml3328_set_scale,
	.get_scale = veml3328_get_scale,
	.set_data_rate = veml3328_set_data_rate,
	.get_data_rate = veml3328_get_data_rate,
	.perform_calib = veml3328_perform_calib,
};

/*
 * ============== The RGB portion of the driver ==============
 */

static int veml3328_rgb_read(const struct motion_sensor_t *s, intv3_t v)
{
	struct veml3328_rgb_drv_data_t *drv_data = VEML3328_RGB_DRV_DATA(s);
	struct veml3328_calib *calib = &(drv_data->calib);
	int r, g, b, c;
	int addr = s->i2c_spi_addr_flags;
	int port = s->port;
	float CCTi, x, y;
	float X, Y, Z;

	RETURN_ERROR(i2c_read16(port, addr, VEML3328_REG_C, &c));
	RETURN_ERROR(i2c_read16(port, addr, VEML3328_REG_R, &r));
	RETURN_ERROR(i2c_read16(port, addr, VEML3328_REG_G, &g));
	RETURN_ERROR(i2c_read16(port, addr, VEML3328_REG_B, &b));

	if (drv_data->calibration_mode) {
		v[0] = r;
		v[1] = g;
		v[2] = b;
		return EC_SUCCESS;
	}

	/* XYZ conversion */

	c = MAX(c, 1);
	g = MAX(g, 1);
	if ((r + g - b) <= 0)
		CCTi = 0.1;
	else
		CCTi = (r + g - b) / (float)c;

	if ((r < VEML3328_MIN_LIGHT_THRES) || (g < VEML3328_MIN_LIGHT_THRES) ||
	    (b < VEML3328_MIN_LIGHT_THRES) || (c < VEML3328_MIN_LIGHT_THRES)) {
		/* low lux, assume the light is white */
		x = 0.362;
		y = 0.366;
	} else if ((r >= VEML3328_MAX_LIGHT_THRES) ||
		   (g >= VEML3328_MAX_LIGHT_THRES) ||
		   (b >= VEML3328_MAX_LIGHT_THRES) ||
		   (c >= VEML3328_MAX_LIGHT_THRES)) {
		/* high lux, assume the light is white */
		x = 0.362;
		y = 0.366;
	} else {
		x = calib->A2 * CCTi * CCTi + calib->A1 * CCTi + calib->A0;
		y = calib->B2 * CCTi * CCTi + calib->B1 * CCTi + calib->B0;

		if (x < calib->Dx_min)
			x = calib->Dx_min;
		if (x > calib->Dx_max)
			x = calib->Dx_max;
		if (y < calib->Dy_min)
			y = calib->Dy_min;
		if (y > calib->Dy_max)
			y = calib->Dy_max;
	}

	/* Avoid any chance of crashing */
	if (y == 0)
		return EC_ERROR_INVAL;

	/* Y is lux */
	Y = calib->LG * g / VEML3328_DEFAULT_GAIN;
	X = Y * (x / y);
	Z = (Y / y) - X - Y;

	/* Non-negative Z, as suggested in b/312586806#comment41 */
	Z = MAX(Z, 0);

	v[0] = (int)X;
	v[1] = (int)Y;
	v[2] = (int)Z;

	return EC_SUCCESS;
}

static int veml3328_rgb_set_range(struct motion_sensor_t *s, int range, int rnd)
{
	return EC_SUCCESS;
}

static int veml3328_rgb_set_offset(const struct motion_sensor_t *s,
				   const int16_t *offset, int16_t temp)
{
	/* Do not allow offset to be changed, it's predetermined */
	return EC_SUCCESS;
}

static int veml3328_rgb_get_offset(const struct motion_sensor_t *s,
				   int16_t *offset, int16_t *temp)
{
	offset[X] = VEML3328_RGB_DRV_DATA(s)->calibration.rgb_cal[X].offset;
	offset[Y] = VEML3328_RGB_DRV_DATA(s)->calibration.rgb_cal[Y].offset;
	offset[Z] = VEML3328_RGB_DRV_DATA(s)->calibration.rgb_cal[Z].offset;

	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static int veml3328_rgb_set_scale(const struct motion_sensor_t *s,
				  const uint16_t *scale, int16_t temp)
{
	struct rgb_channel_calibration_t *rgb_cal =
		VEML3328_RGB_DRV_DATA(s)->calibration.rgb_cal;

	if (scale[X] == 0 || scale[Y] == 0 || scale[Z] == 0)
		return EC_ERROR_INVAL;

	rgb_cal[RED_RGB_IDX].scale.k_channel_scale = scale[X];
	rgb_cal[GREEN_RGB_IDX].scale.k_channel_scale = scale[Y];
	rgb_cal[BLUE_RGB_IDX].scale.k_channel_scale = scale[Z];

	return EC_SUCCESS;
}

static int veml3328_rgb_get_scale(const struct motion_sensor_t *s,
				  uint16_t *scale, int16_t *temp)
{
	struct rgb_channel_calibration_t *rgb_cal =
		VEML3328_RGB_DRV_DATA(s)->calibration.rgb_cal;

	scale[X] = rgb_cal[RED_RGB_IDX].scale.k_channel_scale;
	scale[Y] = rgb_cal[GREEN_RGB_IDX].scale.k_channel_scale;
	scale[Z] = rgb_cal[BLUE_RGB_IDX].scale.k_channel_scale;
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;

	return EC_SUCCESS;
}

static int veml3328_rgb_set_data_rate(const struct motion_sensor_t *s, int rate,
				      int roundup)
{
	return EC_SUCCESS;
}

static int veml3328_rgb_get_data_rate(const struct motion_sensor_t *s)
{
	/* Clear ALS should be defined before RGB sensor */
	return veml3328_get_data_rate(s - 1);
}

static int veml3328_rgb_init(struct motion_sensor_t *s)
{
	return EC_SUCCESS;
}

const struct accelgyro_drv veml3328_rgb_drv = {
	.init = veml3328_rgb_init,
	.read = veml3328_rgb_read,
	.set_range = veml3328_rgb_set_range,
	.set_offset = veml3328_rgb_set_offset,
	.get_offset = veml3328_rgb_get_offset,
	.set_scale = veml3328_rgb_set_scale,
	.get_scale = veml3328_rgb_get_scale,
	.set_data_rate = veml3328_rgb_set_data_rate,
	.get_data_rate = veml3328_rgb_get_data_rate,
};
