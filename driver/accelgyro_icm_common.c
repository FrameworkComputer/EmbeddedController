/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * ICM accelerometer and gyroscope module for Chrome EC
 * 3D digital accelerometer & 3D digital gyroscope
 */

#include "accelgyro.h"
#include "console.h"
#include "i2c.h"
#include "spi.h"
#include "driver/accelgyro_icm_common.h"
#include "driver/accelgyro_icm426xx.h"

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ## args)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ## args)

#ifdef CONFIG_SPI_ACCEL_PORT
static int icm_spi_raw_read(const int addr, const uint8_t reg,
			    uint8_t *data, const int len)
{
	uint8_t cmd = 0x80 | reg;

	return spi_transaction(&spi_devices[addr], &cmd, 1, data, len);
}

static int icm_spi_raw_write(const int addr, const uint8_t reg,
			     const uint8_t *data, const int len)
{
	uint8_t cmd[3];
	int i;

	if (len > 2)
		return EC_ERROR_UNIMPLEMENTED;

	cmd[0] = reg;
	for (i = 0; i < len; ++i)
		cmd[i + 1] = data[i];

	return spi_transaction(&spi_devices[addr], cmd, len + 1, NULL, 0);
}
#endif

static int icm_bank_sel(const struct motion_sensor_t *s, const int reg)
{
	struct icm_drv_data_t *st = ICM_GET_DATA(s);
	uint8_t bank = ICM426XX_REG_GET_BANK(reg);
	int ret;

	if (bank == st->bank)
		return EC_SUCCESS;

	ret = EC_ERROR_UNIMPLEMENTED;
	if (SLAVE_IS_SPI(s->i2c_spi_addr_flags)) {
#ifdef CONFIG_SPI_ACCEL_PORT
		ret = icm_spi_raw_write(
				SLAVE_GET_SPI_ADDR(s->i2c_spi_addr_flags),
				ICM426XX_REG_BANK_SEL, &bank, 1);
#endif
	} else {
#ifdef I2C_PORT_ACCEL
		ret = i2c_write8(s->port, s->i2c_spi_addr_flags,
				 ICM426XX_REG_BANK_SEL, bank);
#endif
	}

	if (ret == EC_SUCCESS)
		st->bank = bank;

	return ret;
}

/**
 * Read 8 bits register
 */
int icm_read8(const struct motion_sensor_t *s, const int reg, int *data_ptr)
{
	const uint8_t addr = ICM426XX_REG_GET_ADDR(reg);
	int ret;

	ret = icm_bank_sel(s, reg);
	if (ret != EC_SUCCESS)
		return ret;

	ret = EC_ERROR_UNIMPLEMENTED;
	if (SLAVE_IS_SPI(s->i2c_spi_addr_flags)) {
#ifdef CONFIG_SPI_ACCEL_PORT
		uint8_t val;

		ret = icm_spi_raw_read(
				SLAVE_GET_SPI_ADDR(s->i2c_spi_addr_flags),
				addr, &val, sizeof(val));
		if (ret == EC_SUCCESS)
			*data_ptr = val;
#endif
	} else {
#ifdef I2C_PORT_ACCEL
		ret = i2c_read8(s->port, s->i2c_spi_addr_flags, addr, data_ptr);
#endif
	}

	return ret;
}

/**
 * Write 8 bits register
 */
int icm_write8(const struct motion_sensor_t *s, const int reg, int data)
{
	const uint8_t addr = ICM426XX_REG_GET_ADDR(reg);
	int ret;

	ret = icm_bank_sel(s, reg);
	if (ret != EC_SUCCESS)
		return ret;

	ret = EC_ERROR_UNIMPLEMENTED;
	if (SLAVE_IS_SPI(s->i2c_spi_addr_flags)) {
#ifdef CONFIG_SPI_ACCEL_PORT
		uint8_t val = data;

		ret = icm_spi_raw_write(
				LAVE_GET_SPI_ADDR(s->i2c_spi_addr_flags),
				addr, &val, sizeof(val));
#endif
	} else {
#ifdef I2C_PORT_ACCEL
		ret = i2c_write8(s->port, s->i2c_spi_addr_flags, addr, data);
#endif
	}

	return ret;
}

/**
 * Read 16 bits register
 */
int icm_read16(const struct motion_sensor_t *s, const int reg, int *data_ptr)
{
	const uint8_t addr = ICM426XX_REG_GET_ADDR(reg);
	int ret;

	ret = icm_bank_sel(s, reg);
	if (ret != EC_SUCCESS)
		return ret;

	ret = EC_ERROR_UNIMPLEMENTED;
	if (SLAVE_IS_SPI(s->i2c_spi_addr_flags)) {
#ifdef CONFIG_SPI_ACCEL_PORT
		uint8_t val[2];

		ret = icm_spi_raw_read(
				LAVE_GET_SPI_ADDR(s->i2c_spi_addr_flags),
				addr, val, sizeof(val));
		if (ret == EC_SUCCESS) {
			if (I2C_IS_BIG_ENDIAN(s->i2c_spi_addr_flags))
				*data_ptr = ((int)val[0] << 8) | val[1];
			else
				*data_ptr = ((int)val[1] << 8) | val[0];
		}
#endif
	} else {
#ifdef I2C_PORT_ACCEL
		ret = i2c_read16(s->port, s->i2c_spi_addr_flags,
				addr, data_ptr);
#endif
	}

	return ret;
}

/**
 * Write 16 bits register
 */
int icm_write16(const struct motion_sensor_t *s, const int reg, int data)
{
	const uint8_t addr = ICM426XX_REG_GET_ADDR(reg);
	int ret;

	ret = icm_bank_sel(s, reg);
	if (ret != EC_SUCCESS)
		return ret;

	ret = EC_ERROR_UNIMPLEMENTED;
	if (SLAVE_IS_SPI(s->i2c_spi_addr_flags)) {
#ifdef CONFIG_SPI_ACCEL_PORT
		uint8_t val[2];

		if (I2C_IS_BIG_ENDIAN(s->i2c_spi_addr_flags)) {
			val[0] = (data >> 8) & 0xFF;
			val[1] = data & 0xFF;
		} else {
			val[0] = data & 0xFF;
			val[1] = (data >> 8) & 0xFF;
		}
		ret = icm_spi_raw_write(
				LAVE_GET_SPI_ADDR(s->i2c_spi_addr_flags),
				addr, val, sizeof(val));
#endif
	} else {
#ifdef I2C_PORT_ACCEL
		ret = i2c_write16(s->port, s->i2c_spi_addr_flags, addr, data);
#endif
	}

	return ret;
}

/**
 * Read n bytes
 */
int icm_read_n(const struct motion_sensor_t *s, const int reg,
	       uint8_t *data_ptr, const int len)
{
	const uint8_t addr = ICM426XX_REG_GET_ADDR(reg);
	int ret;

	ret = icm_bank_sel(s, reg);
	if (ret != EC_SUCCESS)
		return ret;

	ret = EC_ERROR_UNIMPLEMENTED;
	if (SLAVE_IS_SPI(s->i2c_spi_addr_flags)) {
#ifdef CONFIG_SPI_ACCEL_PORT
		ret = icm_spi_raw_read(
				SLAVE_GET_SPI_ADDR(s->i2c_spi_addr_flags),
				addr, data_ptr, len);
#endif
	} else {
#ifdef I2C_PORT_ACCEL
		ret = i2c_read_block(s->port, s->i2c_spi_addr_flags, addr,
				     data_ptr, len);
#endif
	}

	return ret;
}

int icm_field_update8(const struct motion_sensor_t *s, const int reg,
		      const uint8_t field_mask, const uint8_t set_value)
{
	const uint8_t addr = ICM426XX_REG_GET_ADDR(reg);
	int ret;

	ret = icm_bank_sel(s, reg);
	if (ret != EC_SUCCESS)
		return ret;

	ret = EC_ERROR_UNIMPLEMENTED;
	if (SLAVE_IS_SPI(s->i2c_spi_addr_flags)) {
#ifdef CONFIG_SPI_ACCEL_PORT
		uint8_t val;

		ret = icm_spi_raw_read(
				SLAVE_GET_SPI_ADDR(s->i2c_spi_addr_flags),
				addr, &val, sizeof(val));
		if (ret != EC_SUCCESS)
			return ret;

		val = (val & (~field_mask)) | set_value;

		ret = icm_spi_raw_write(
				SLAVE_GET_SPI_ADDR(s->i2c_spi_addr_flags),
				addr, &val, sizeof(val));
#endif
	} else {
#ifdef I2C_PORT_ACCEL
		ret = i2c_field_update8(s->port, s->i2c_spi_addr_flags, addr,
					field_mask, set_value);
#endif
	}

	return ret;
}

int icm_get_resolution(const struct motion_sensor_t *s)
{
	return ICM_RESOLUTION;
}

int icm_get_range(const struct motion_sensor_t *s)
{
	struct accelgyro_saved_data_t *data = ICM_GET_SAVED_DATA(s);

	return data->range;
}

int icm_get_data_rate(const struct motion_sensor_t *s)
{
	struct accelgyro_saved_data_t *data = ICM_GET_SAVED_DATA(s);

	return data->odr;
}

int icm_set_scale(const struct motion_sensor_t *s, const uint16_t *scale,
		  int16_t temp)
{
	struct accelgyro_saved_data_t *data = ICM_GET_SAVED_DATA(s);

	data->scale[X] = scale[X];
	data->scale[Y] = scale[Y];
	data->scale[Z] = scale[Z];
	return EC_SUCCESS;
}

int icm_get_scale(const struct motion_sensor_t *s, uint16_t *scale,
		  int16_t *temp)
{
	struct accelgyro_saved_data_t *data = ICM_GET_SAVED_DATA(s);

	scale[X] = data->scale[X];
	scale[Y] = data->scale[Y];
	scale[Z] = data->scale[Z];
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

/* FIFO header: 1 byte */
#define ICM_FIFO_HEADER_MSG		BIT(7)
#define ICM_FIFO_HEADER_ACCEL		BIT(6)
#define ICM_FIFO_HEADER_GYRO		BIT(5)
#define ICM_FIFO_HEADER_TMST_FSYNC	GENMASK(3, 2)
#define ICM_FIFO_HEADER_ODR_ACCEL	BIT(1)
#define ICM_FIFO_HEADER_ODR_GYRO	BIT(0)

/* FIFO data packet */
struct icm_fifo_sensor_data {
	int16_t x;
	int16_t y;
	int16_t z;
} __packed;

struct icm_fifo_1sensor_packet {
	uint8_t header;
	struct icm_fifo_sensor_data data;
	int8_t temp;
} __packed;
#define ICM_FIFO_1SENSOR_PACKET_SIZE	8

struct icm_fifo_2sensors_packet {
	uint8_t header;
	struct icm_fifo_sensor_data accel;
	struct icm_fifo_sensor_data gyro;
	int8_t temp;
	uint16_t timestamp;
} __packed;
#define ICM_FIFO_2SENSORS_PACKET_SIZE	16

ssize_t icm_fifo_decode_packet(const void *packet, const uint8_t **accel,
		const uint8_t **gyro)
{
	const struct icm_fifo_1sensor_packet *pack1 = packet;
	const struct icm_fifo_2sensors_packet *pack2 = packet;
	uint8_t header = *((const uint8_t *)packet);

	/* FIFO empty */
	if (header & ICM_FIFO_HEADER_MSG) {
		if (accel != NULL)
			*accel = NULL;
		if (gyro != NULL)
			*gyro = NULL;
		return 0;
	}

	/* accel + gyro */
	if ((header & ICM_FIFO_HEADER_ACCEL) &&
	    (header & ICM_FIFO_HEADER_GYRO)) {
		if (accel != NULL)
			*accel = (uint8_t *)&pack2->accel;
		if (gyro != NULL)
			*gyro = (uint8_t *)&pack2->gyro;
		return ICM_FIFO_2SENSORS_PACKET_SIZE;
	}

	/* accel only */
	if (header & ICM_FIFO_HEADER_ACCEL) {
		if (accel != NULL)
			*accel = (uint8_t *)&pack1->data;
		if (gyro != NULL)
			*gyro = NULL;
		return ICM_FIFO_1SENSOR_PACKET_SIZE;
	}

	/* gyro only */
	if (header & ICM_FIFO_HEADER_GYRO) {
		if (accel != NULL)
			*accel = NULL;
		if (gyro != NULL)
			*gyro = (uint8_t *)&pack1->data;
		return ICM_FIFO_1SENSOR_PACKET_SIZE;
	}

	/* invalid packet if here */
	return -EC_ERROR_INVAL;
}
