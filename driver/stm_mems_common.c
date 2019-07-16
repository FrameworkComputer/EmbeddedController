/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * Commons acc/gyro function for ST sensors in Chrome EC
 */
#include "stm_mems_common.h"

/**
 * st_raw_read_n - Read n bytes for read
 */
int st_raw_read_n(const int port,
		  const uint16_t i2c_addr_flags,
		  const uint8_t reg, uint8_t *data_ptr, const int len)
{
	/* TODO: Implement SPI interface support */
	return i2c_read_block(port, i2c_addr_flags,
			      reg | 0x80, data_ptr, len);
}

/**
 * st_raw_read_n_noinc - Read n bytes for read (no auto inc address)
 */
int st_raw_read_n_noinc(const int port,
			const uint16_t i2c_addr_flags,
			const uint8_t reg, uint8_t *data_ptr, const int len)
{
	/* TODO: Implement SPI interface support */
	return i2c_read_block(port, i2c_addr_flags,
			      reg, data_ptr, len);
}

 /**
 * st_write_data_with_mask - Write register with mask
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

	err = st_raw_read8(s->port, s->i2c_spi_addr_flags,
			   reg, &old_data);
	if (err != EC_SUCCESS)
		return err;

	new_data = ((old_data & (~mask)) |
		    ((data << __builtin_ctz(mask)) & mask));

	if (new_data == old_data)
		return EC_SUCCESS;

	return st_raw_write8(s->port, s->i2c_spi_addr_flags,
			     reg, new_data);
}

/**
 * st_get_resolution - Get bit resolution
 * @s: Motion sensor pointer
 */
int st_get_resolution(const struct motion_sensor_t *s)
{
	struct stprivate_data *data = s->drv_data;

	return data->resol;
}

/**
 * st_set_offset - Set data offset
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
 * st_get_offset - Get data offset
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
 * st_get_data_rate - Get data rate (ODR)
 * @s: Motion sensor pointer
 */
int st_get_data_rate(const struct motion_sensor_t *s)
{
	struct stprivate_data *data = s->drv_data;

	return data->base.odr;
}

/**
 * st_normalize - Apply LSB data sens. and rotation based on sensor resolution
 * @s: Motion sensor pointer
 * @v: output vector
 * @data: LSB raw data
 */
void st_normalize(const struct motion_sensor_t *s, intv3_t v, uint8_t *data)
{
	int i, range;
	struct stprivate_data *drvdata = s->drv_data;
	/*
	 * Data is left-aligned and the bottom bits need to be
	 * cleared because they may contain trash data.
	 */
	uint16_t mask = ~((1 << (16 - drvdata->resol)) - 1);

	for (i = X; i <= Z; i++) {
		v[i] = ((data[i * 2 + 1] << 8) | data[i * 2]) & mask;
	}

	rotate(v, *s->rot_standard_ref, v);

	/* apply offset in the device coordinates */
	range = s->drv->get_range(s);
	for (i = X; i <= Z; i++)
		v[i] += (drvdata->offset[i] << 5) / range;
}
