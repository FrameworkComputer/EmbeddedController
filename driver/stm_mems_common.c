/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * Commons acc/gyro function for ST sensors in Chrome EC
 */
#include "stm_mems_common.h"

/**
 * Read single register
 */
int raw_read8(const int port, const int addr, const int reg, int *data_ptr)
{
	/* TODO: Implement SPI interface support */
	return i2c_read8(port, addr, reg, data_ptr);
}

/**
 * Write single register
 */
int raw_write8(const int port, const int addr, const int reg, int data)
{
	/* TODO: Implement SPI interface support */
	return i2c_write8(port, addr, reg, data);
}

/**
 * Read n bytes for read
 * NOTE: Some chip use MSB for auto-increments in SUB address
 * MSB must be set for autoincrement in multi read when auto_inc
 * is set
 */
int st_raw_read_n(const int port, const int addr, const uint8_t reg,
	       uint8_t *data_ptr, const int len)
{
	int rv = -EC_ERROR_PARAM1;
	uint8_t reg_a = reg | 0x80;

	/* TODO: Implement SPI interface support */
	i2c_lock(port, 1);
	rv = i2c_xfer(port, addr, &reg_a, 1, data_ptr, len, I2C_XFER_SINGLE);
	i2c_lock(port, 0);

	return rv;
}

 /**
 * write_data_with_mask - Write register with mask
 * @s: Motion sensor pointer
 * @reg: Device register
 * @mask: The mask to search
 * @data: Data pointer
 */
int st_write_data_with_mask(const struct motion_sensor_t *s, int reg,
			 uint8_t mask, uint8_t data)
{
	int err;
	int new_data = 0x00, old_data = 0x00;

	err = raw_read8(s->port, s->addr, reg, &old_data);
	if (err != EC_SUCCESS)
		return err;

	new_data = ((old_data & (~mask)) |
		    ((data << __builtin_ctz(mask)) & mask));

	if (new_data == old_data)
		return EC_SUCCESS;

	return raw_write8(s->port, s->addr, reg, new_data);
}

 /**
 * get_resolution - Get bit resolution
 * @s: Motion sensor pointer
 *
 * TODO: must support multiple resolution
 */
int st_get_resolution(const struct motion_sensor_t *s)
{
	struct stprivate_data *data = s->drv_data;

	return data->resol;
}

/**
 * set_offset - Set data offset
 * @s: Motion sensor pointer
 * @offset: offset vector
 * @temp: Temp
 */
int st_set_offset(const struct motion_sensor_t *s,
	       const int16_t *offset, int16_t temp)
{
	struct stprivate_data *data = s->drv_data;

	data->offset[X] = offset[X];
	data->offset[Y] = offset[Y];
	data->offset[Z] = offset[Z];
	return EC_SUCCESS;
}

/**
 * get_offset - Get data offset
 * @s: Motion sensor pointer
 * @offset: offset vector
 * @temp: Temp
 */
int st_get_offset(const struct motion_sensor_t *s,
	       int16_t *offset, int16_t *temp)
{
	struct stprivate_data *data = s->drv_data;

	offset[X] = data->offset[X];
	offset[Y] = data->offset[Y];
	offset[Z] = data->offset[Z];
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

/**
 * get_data_rate - Get data rate (ODR)
 * @s: Motion sensor pointer
 */
int st_get_data_rate(const struct motion_sensor_t *s)
{
	struct stprivate_data *data = s->drv_data;

	return data->base.odr;
}

/**
 * normalize - Apply LSB data sens. and rotation based on sensor resolution
 * @s: Motion sensor pointer
 * @v: output vector
 * @data: LSB raw data
 */
void st_normalize(const struct motion_sensor_t *s, vector_3_t v, uint8_t *data)
{
	int i;
	struct stprivate_data *drvdata = s->drv_data;

	/* Adjust data to sensor Sensitivity and Precision:
	 * - devices with 16 bits resolution has gain in ug/LSB
	 * - devices with 8/10 bits resolution has gain in mg/LSB
	 */
	for (i = 0; i < 3; i++) {
		switch (drvdata->resol) {
		case 10:
			v[i] = ((int16_t)((data[i * 2 + 1] << 8) |
				   data[i * 2]) >> 6);
			v[i] = v[i] * drvdata->base.range;
			break;
		case 16:
			v[i] = ((int16_t)(data[i * 2 + 1] << 8) |
				    data[i * 2]);
			v[i] = (v[i] * drvdata->base.range) / 1000;
			break;
		}
	}

	rotate(v, *s->rot_standard_ref, v);
}
