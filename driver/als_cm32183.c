/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * CAPELLA CM32181E light sensor driver
 */
#include "common.h"
#include "driver/als_cm32183.h"
#include "i2c.h"
#include "accelgyro.h"
#include "math_util.h"
#define CM32183_GET_DATA(_s)	((struct cm32183_drv_data *)(_s)->drv_data)
/*
 * Read data from CM32183 light sensor, and transfer unit into lux.
 */
static int cm32183_read_lux(const struct motion_sensor_t *s, intv3_t v)
{
	struct cm32183_drv_data *drv_data = CM32183_GET_DATA(s);
	int ret;
	int data;

	ret = i2c_read16(s->port, s->i2c_spi_addr_flags,
		CM32183_REG_ALS_RESULT, &data);
	if (ret)
		return ret;
	/*
	 * lux = data * 0.016
	 */
	data = (data * 16)/1000;
	data += drv_data->offset;
	if (data < 0)
		data = 1;
	v[0] = data;
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
static int cm32183_set_range(const struct motion_sensor_t *s, int range,
			     int rnd)
{
	return EC_SUCCESS;
}
static int cm32183_set_data_rate(const struct motion_sensor_t *s,
				int rate, int roundup)
{
	CM32183_GET_DATA(s)->rate = rate;
	return EC_SUCCESS;
}
static int cm32183_get_data_rate(const struct motion_sensor_t *s)
{
	return CM32183_GET_DATA(s)->rate;
}
static int cm32183_set_offset(const struct motion_sensor_t *s,
			const int16_t *offset, int16_t temp)
{
	return EC_SUCCESS;
}
static int cm32183_get_offset(const struct motion_sensor_t *s,
			int16_t *offset, int16_t *temp)
{
	*offset = CM32183_GET_DATA(s)->offset;
	return EC_SUCCESS;
}

#ifdef CONFIG_ACCEL_INTERRUPTS
static int cm32183_irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	int ret;
	int data;

	ret = i2c_read16(s->port, s->i2c_spi_addr_flags,
		CM32183_REG_ALS_RESULT, &data);
	if (ret)
		return ret;
	/*
	 * lux = data * 0.016
	 */
	data = (data * 16)/1000;
	s->xyz[0] = data;
	s->xyz[1] = 0;
	s->xyz[2] = 0;
	return EC_SUCCESS;
}
#endif

/**
 * Initialise CM32181E light sensor.
 */
static int cm32183_init(const struct motion_sensor_t *s)
{
	int ret;
	int data;

	ret = i2c_write16(s->port, s->i2c_spi_addr_flags,
		CM32183_REG_CONFIGURE, 0x0004);
	if (ret)
		return ret;
	ret = i2c_read16(s->port, s->i2c_spi_addr_flags,
		CM32183_REG_ALS_RESULT, &data);
	if (ret)
		return ret;
	return EC_SUCCESS;
}
const struct accelgyro_drv cm32183_drv = {
	.init = cm32183_init,
	.read = cm32183_read_lux,
	.set_range = cm32183_set_range,
	.set_offset = cm32183_set_offset,
	.get_offset = cm32183_get_offset,
	.set_data_rate = cm32183_set_data_rate,
	.get_data_rate = cm32183_get_data_rate,
#ifdef CONFIG_ACCEL_INTERRUPTS
	.irq_handler = cm32183_irq_handler,
#endif
};
