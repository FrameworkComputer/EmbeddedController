/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Rohm BH1730 Ambient light sensor driver
 */

#include "accelgyro.h"
#include "config.h"
#include "console.h"
#include "driver/als_bh1730.h"
#include "i2c.h"

#define CPRINTS(format, args...) cprints(CC_MOTION_SENSE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_MOTION_SENSE, format, ## args)

/**
 * Convert BH1730 data0, data1 to lux
 */
static int bh1730_convert_to_lux(uint32_t data0_1)
{
	int lux;
	uint16_t data0 = 0x0000ffff & data0_1;
	uint16_t data1 = data0_1 >> 16;
	uint32_t d0_1k = data0 * 1000;
	uint32_t d1_1k = data1 * 1000;
	uint32_t d_temp;
	uint32_t d_lux;

	if (data0 == 0)
		return 0;
	else
		d_temp = d1_1k / data0;

	if(d_temp < BH1730_LUXTH1_1K) {
		d0_1k = BH1730_LUXTH1_D0_1K * data0;
		d1_1k = BH1730_LUXTH1_D1_1K * data1;
	} else if(d_temp < BH1730_LUXTH2_1K) {
		d0_1k = BH1730_LUXTH2_D0_1K * data0;
		d1_1k = BH1730_LUXTH2_D1_1K * data1;
	} else if(d_temp < BH1730_LUXTH3_1K) {
		d0_1k = BH1730_LUXTH3_D0_1K * data0;
		d1_1k = BH1730_LUXTH3_D1_1K * data1;
	} else if(d_temp < BH1730_LUXTH4_1K) {
		d0_1k = BH1730_LUXTH4_D0_1K * data0;
		d1_1k = BH1730_LUXTH4_D1_1K * data1;
	} else
		return 0;

	d_lux = (d0_1k - d1_1k) / BH1730_GAIN_DIV;
	d_lux *= 100;
	lux = d_lux / ITIME_MS_X_1K;

	return lux;
}

/**
 * Read BH1730 light sensor data.
 */
static int bh1730_read_lux(const struct motion_sensor_t *s, intv3_t v)
{
	struct bh1730_drv_data_t *drv_data = BH1730_GET_DATA(s);
	int ret;
	int data0_1;

	/* read data0 and data1 from sensor */
	ret = i2c_read32(s->port, s->i2c_spi_addr_flags,
			 BH1730_DATA0LOW, &data0_1);
	if (ret != EC_SUCCESS) {
		CPRINTF("bh1730_read_lux - fail %d\n", ret);
		return ret;
	}

	/* convert sensor data0 and data1 to lux */
	v[0] = bh1730_convert_to_lux(data0_1);
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

static int bh1730_set_range(const struct motion_sensor_t *s, int range,
			     int rnd)
{
	return EC_SUCCESS;
}

static int bh1730_get_range(const struct motion_sensor_t *s)
{
	return 1;
}

static int bh1730_set_data_rate(const struct motion_sensor_t *s,
				int rate, int roundup)
{
	struct bh1730_drv_data_t *drv_data = BH1730_GET_DATA(s);

	/* now only one rate supported */
	drv_data->rate = BH1730_10000_MHZ;

	return EC_SUCCESS;
}

static int bh1730_get_data_rate(const struct motion_sensor_t *s)
{
	struct bh1730_drv_data_t *drv_data = BH1730_GET_DATA(s);

	return drv_data->rate;
}

static int bh1730_set_offset(const struct motion_sensor_t *s,
			const int16_t *offset,
			int16_t    temp)
{
	return EC_SUCCESS;
}

static int bh1730_get_offset(const struct motion_sensor_t *s,
			int16_t   *offset,
			int16_t    *temp)
{
	*offset = 0;

	return EC_SUCCESS;
}

/**
 * Initialise BH1730 Ambient light sensor.
 */
static int bh1730_init(const struct motion_sensor_t *s)
{
	int ret;

	/* power and measurement bit high */
	ret = i2c_write8(s->port, s->i2c_spi_addr_flags,
			BH1730_CONTROL,
			BH1730_CONTROL_POWER_ENABLE
			      | BH1730_CONTROL_ADC_EN_ENABLE);

	if (ret != EC_SUCCESS) {
		CPRINTF("bh1730_init_sensor - enable fail %d\n", ret);
		return ret;
	}

	/* set timing */
	ret = i2c_write8(s->port, s->i2c_spi_addr_flags,
			 BH1730_TIMING, BH1730_CONF_ITIME);
	if (ret != EC_SUCCESS) {
		CPRINTF("bh1730_init_sensor - time fail %d\n", ret);
		return ret;
	}
	/* set ADC gain */
	ret = i2c_write8(s->port, s->i2c_spi_addr_flags,
			 BH1730_GAIN, BH1730_CONF_GAIN);

	if (ret != EC_SUCCESS) {
		CPRINTF("bh1730_init_sensor - gain fail %d\n", ret);
		return ret;
	}

	return sensor_init_done(s);
}

const struct accelgyro_drv bh1730_drv = {
	.init = bh1730_init,
	.read = bh1730_read_lux,
	.set_range = bh1730_set_range,
	.get_range = bh1730_get_range,
	.set_offset = bh1730_set_offset,
	.get_offset = bh1730_get_offset,
	.set_data_rate = bh1730_set_data_rate,
	.get_data_rate = bh1730_get_data_rate,
};

