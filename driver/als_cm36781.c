/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Vishay CM36781 light sensor driver
 */

#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "driver/als_cm36781.h"
#include "i2c.h"
#include "math_util.h"

struct cm36781_drv_data {
	int rate;
	int last_value;
	/* the coefficient is scale.uscale */
	int16_t scale;
	uint16_t uscale;
	int16_t offset;
};

#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ##args)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ##args)
#define CM36781_GET_DATA(_s) ((struct cm36781_drv_data *)(_s)->drv_data)

/*
 * Read CM36781 light sensor data.
 */
static int cm36781_read_lux(int *lux)
{
	int ret;
	int data;

	ret = i2c_read16(I2C_PORT_SENSOR, CM36781_I2C_ADDR_FLAGS,
			 CM36781_ALS_DATA, &data);
	if (ret != EC_SUCCESS) {
		CPRINTS("cm36781 failed reading ALS_DATA reg ret=%d", ret);
		return ret;
	}
	/*
	 * lux = data * 0.024
	 */
	*lux = (data * 24) / 1000;

	return EC_SUCCESS;
}

/*
 * Read data from CM36781 light sensor, and transfer unit into lux.
 */
static int cm36781_read(const struct motion_sensor_t *s, intv3_t v)
{
	struct cm36781_drv_data *drv_data = CM36781_GET_DATA(s);
	int ret;
	int lux_data;

	ret = cm36781_read_lux(&lux_data);
	if (ret != EC_SUCCESS)
		return ret;

	lux_data += drv_data->offset;
	lux_data = lux_data * drv_data->scale +
		   lux_data * drv_data->uscale / 10000;

	v[0] = lux_data;
	v[1] = 0;
	v[2] = 0;

	/*
	 * Return an error when nothing change to prevent filling the
	 * fifo with useless data.
	 */
	if (v[0] == drv_data->last_value)
		return EC_ERROR_UNCHANGED;

	drv_data->last_value = v[0];

	return EC_SUCCESS;
}

static int cm36781_set_range(struct motion_sensor_t *s, int range, int rnd)
{
	return EC_SUCCESS;
}

static int cm36781_set_offset(const struct motion_sensor_t *s,
			      const int16_t *offset, int16_t temp)
{
	/* TODO: check calibration method */
	return EC_SUCCESS;
}

static int cm36781_get_offset(const struct motion_sensor_t *s, int16_t *offset,
			      int16_t *temp)
{
	*offset = CM36781_GET_DATA(s)->offset;
	return EC_SUCCESS;
}

static int cm36781_set_data_rate(const struct motion_sensor_t *s, int rate,
				 int roundup)
{
	struct cm36781_drv_data *drv_data = CM36781_GET_DATA(s);
	int ret;
	int als_conf;

	if (rate == 0) {
		/*
		 * Suspend driver:
		 */
		als_conf = CM36781_ALS_CONF_DEFAULT | CM36781_ALS_SD;
	} else {
		als_conf = CM36781_ALS_CONF_DEFAULT;
		/*
		 * We set the sensor for continuous mode,
		 * integrating over 50ms.
		 * Do not allow range higher than 10Hz.
		 */
		if (rate > CM36781_MAX_FREQ)
			rate = CM36781_MAX_FREQ;
	}
	ret = i2c_write16(s->port, s->i2c_spi_addr_flags, CM36781_ALS_CONF,
			  als_conf);
	if (ret)
		return ret;
	drv_data->rate = rate;
	return EC_SUCCESS;
}

static int cm36781_get_data_rate(const struct motion_sensor_t *s)
{
	return CM36781_GET_DATA(s)->rate;
}

/**
 * Initialise CM36781 light sensor.
 */
static int cm36781_init(struct motion_sensor_t *s)
{
	int ret;
	int id;

	/* Check chip ID */
	ret = i2c_read16(s->port, s->i2c_spi_addr_flags, CM36781_ID, &id);
	if (ret != EC_SUCCESS) {
		CPRINTS("cm36781 failed reading ID reg ret=%d", ret);
		return ret;
	}

	id &= CM36781_DEV_ID_MASK;
	if (id != CM36781_DEV_ID) {
		CPRINTS("cm36781 wrong chip ID=%d", id);
		return EC_ERROR_INVAL;
	}

	/* Power on, write default config */
	ret = i2c_write16(s->port, s->i2c_spi_addr_flags, CM36781_ALS_CONF,
			  CM36781_ALS_CONF_DEFAULT);
	if (ret != EC_SUCCESS) {
		CPRINTS("cm36781 error writing to ALS_CONF reg %d", ret);
		return ret;
	}

	return sensor_init_done(s);
}

const struct accelgyro_drv cm36781_drv = {
	.init = cm36781_init,
	.read = cm36781_read,
	.set_range = cm36781_set_range,
	.set_offset = cm36781_set_offset,
	.get_offset = cm36781_get_offset,
	.set_data_rate = cm36781_set_data_rate,
	.get_data_rate = cm36781_get_data_rate,
};
