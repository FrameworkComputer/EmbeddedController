/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* BMI accelerometer and gyro common definitions for Chrome EC */

#ifndef __CROS_EC_ACCELGYRO_BMI_COMMON_H
#define __CROS_EC_ACCELGYRO_BMI_COMMON_H

#include "accelgyro.h"
#include "accelgyro_bmi160.h"
#include "accelgyro_bmi260.h"
#include "accelgyro_bmi_common_public.h"
#include "mag_bmm150.h"

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(CONFIG_ACCELGYRO_BMI_COMM_SPI) && \
	!defined(CONFIG_ACCELGYRO_BMI_COMM_I2C)
#error "BMI must use either SPI or I2C communication"
#endif

#define BMI_CONF_REG(_sensor) (0x40 + 2 * (_sensor))
#define BMI_RANGE_REG(_sensor) (0x41 + 2 * (_sensor))

#define BMI_ODR_MASK 0x0F
/* odr = 100 / (1 << (8 - reg)) , within limit */
#define BMI_ODR_0_78HZ 0x01
#define BMI_ODR_100HZ 0x08

#define BMI_REG_TO_ODR(_regval)                                        \
	((_regval) < BMI_ODR_100HZ ? 100000 / (1 << (8 - (_regval))) : \
				     100000 * (1 << ((_regval)-8)))
#define BMI_ODR_TO_REG(_odr)                                             \
	((_odr) < 100000 ? (__builtin_clz(100000 / ((_odr) + 1)) - 24) : \
			   (39 - __builtin_clz((_odr) / 100000)))

enum fifo_header {
	BMI_FH_EMPTY = 0x80,
	BMI_FH_SKIP = 0x40,
	BMI_FH_TIME = 0x44,
	BMI_FH_CONFIG = 0x48
};

#define BMI_FH_MODE_MASK 0xc0
#define BMI_FH_PARM_OFFSET 2
#define BMI_FH_PARM_MASK (0x7 << BMI_FH_PARM_OFFSET)
#define BMI_FH_EXT_MASK 0x03

/* Sensor resolution in number of bits. This sensor has fixed resolution. */
#define BMI_RESOLUTION 16
/* Min and Max sampling frequency in mHz */
#define BMI_ACCEL_MIN_FREQ 12500
#define BMI_ACCEL_MAX_FREQ MOTION_MAX_SENSOR_FREQUENCY(1600000, 100000)
#define BMI_GYRO_MIN_FREQ 25000
#define BMI_GYRO_MAX_FREQ MOTION_MAX_SENSOR_FREQUENCY(3200000, 100000)

enum bmi_running_mode {
	STANDARD_UI_9DOF_FIFO = 0,
	STANDARD_UI_IMU_FIFO = 1,
	STANDARD_UI_IMU = 2,
	STANDARD_UI_ADVANCEPOWERSAVE = 3,
	ACCEL_PEDOMETER = 4,
	APPLICATION_HEAD_TRACKING = 5,
	APPLICATION_NAVIGATION = 6,
	APPLICATION_REMOTE_CONTROL = 7,
	APPLICATION_INDOOR_NAVIGATION = 8,
};

#define BMI_FLAG_SEC_I2C_ENABLED BIT(0)
#define BMI_FIFO_FLAG_OFFSET 4
#define BMI_FIFO_ALL_MASK 7

#define BMI_GET_DATA(_s) ((struct bmi_drv_data_t *)(_s)->drv_data)
#define BMI_GET_SAVED_DATA(_s) (&BMI_GET_DATA(_s)->saved_data[(_s)->type])

#define BMI_ACC_DATA(v) \
	(BMI160_ACC_X_L_G + (v) * (BMI260_ACC_X_L_G - BMI160_ACC_X_L_G))
#define BMI_GYR_DATA(v) \
	(BMI160_GYR_X_L_G + (v) * (BMI260_GYR_X_L_G - BMI160_GYR_X_L_G))
#define BMI_AUX_DATA(v) \
	(BMI160_MAG_X_L_G + (v) * (BMI260_AUX_X_L_G - BMI160_MAG_X_L_G))

#define BMI_FIFO_CONFIG_0(v)    \
	(BMI160_FIFO_CONFIG_0 + \
	 (v) * (BMI260_FIFO_CONFIG_0 - BMI160_FIFO_CONFIG_0))
#define BMI_FIFO_CONFIG_1(v)    \
	(BMI160_FIFO_CONFIG_1 + \
	 (v) * (BMI260_FIFO_CONFIG_1 - BMI160_FIFO_CONFIG_1))
#define BMI_FIFO_SENSOR_EN(v, _sensor)           \
	(BMI160_FIFO_SENSOR_EN(_sensor) +        \
	 (v) * (BMI260_FIFO_SENSOR_EN(_sensor) - \
		BMI160_FIFO_SENSOR_EN(_sensor)))

#define BMI_TEMPERATURE_0(v)    \
	(BMI160_TEMPERATURE_0 + \
	 (v) * (BMI260_TEMPERATURE_0 - BMI160_TEMPERATURE_0))
#define BMI_INVALID_TEMP 0x8000

#define BMI_STATUS(v) (BMI160_STATUS + (v) * (BMI260_STATUS - BMI160_STATUS))
#define BMI_DRDY_OFF(_sensor) (7 - (_sensor))
#define BMI_DRDY_MASK(_sensor) (1 << BMI160_DRDY_OFF(_sensor))

#define BMI_OFFSET_ACC70(v)    \
	(BMI160_OFFSET_ACC70 + \
	 (v) * (BMI260_OFFSET_ACC70 - BMI160_OFFSET_ACC70))
#define BMI_OFFSET_GYR70(v)    \
	(BMI160_OFFSET_GYR70 + \
	 (v) * (BMI260_OFFSET_GYR70 - BMI160_OFFSET_GYR70))
/*
 * There is some bits in this register that differ between BMI160 and BMI260
 * Only use this macro for gyro offset 9:8 (BMI_OFFSET_EN_GYR98 5:0).
 */
#define BMI_OFFSET_EN_GYR98(v)    \
	(BMI160_OFFSET_EN_GYR98 + \
	 (v) * (BMI260_OFFSET_EN_GYR98 - BMI160_OFFSET_EN_GYR98))
#define BMI_OFFSET_GYR98_MASK (BIT(6) - 1)
#define BMI_OFFSET_ACC_MULTI_MG (3900 * 1024)
#define BMI_OFFSET_ACC_DIV_MG 1000000
#define BMI_OFFSET_GYRO_MULTI_MDS (61 * 1024)
#define BMI_OFFSET_GYRO_DIV_MDS 1000

#define BMI_FIFO_LENGTH_0(v)    \
	(BMI160_FIFO_LENGTH_0 + \
	 (v) * (BMI260_FIFO_LENGTH_0 - BMI160_FIFO_LENGTH_0))
#define BMI_FIFO_LENGTH_MASK(v)    \
	(BMI160_FIFO_LENGTH_MASK + \
	 (v) * (BMI260_FIFO_LENGTH_MASK - BMI160_FIFO_LENGTH_MASK))
#define BMI_FIFO_DATA(v) \
	(BMI160_FIFO_DATA + (v) * (BMI260_FIFO_DATA - BMI160_FIFO_DATA))

#define BMI_CMD_REG(v) \
	(BMI160_CMD_REG + (v) * (BMI260_CMD_REG - BMI160_CMD_REG))
#define BMI_CMD_FIFO_FLUSH 0xb0

#define BMI_ACCEL_100HZ 100

/*
 * Struct for pairing an engineering value with the register value for a
 * parameter.
 */
struct bmi_accel_param_pair {
	int val; /* Value in engineering units. */
	int reg_val; /* Corresponding register value. */
};

int bmi_get_xyz_reg(const struct motion_sensor_t *s);

/**
 * @param type   Accel/Gyro
 * @param psize  Size of the table
 *
 * @return       Range table of the type.
 */
const struct bmi_accel_param_pair *
bmi_get_range_table(const struct motion_sensor_t *s, int *psize);

/**
 * @return reg value that matches the given engineering value passed in.
 * The round_up flag is used to specify whether to round up or down.
 * Note, this function always returns a valid reg value. If the request is
 * outside the range of values, it returns the closest valid reg value.
 */
int bmi_get_reg_val(const int eng_val, const int round_up,
		    const struct bmi_accel_param_pair *pairs, const int size);

/**
 * @return engineering value that matches the given reg val
 */
int bmi_get_engineering_val(const int reg_val,
			    const struct bmi_accel_param_pair *pairs,
			    const int size);

/**
 * Read 8bit register from accelerometer.
 */
int bmi_read8(const int port, const uint16_t i2c_spi_addr_flags, const int reg,
	      int *data_ptr);

/**
 * Write 8bit register from accelerometer.
 */
int bmi_write8(const int port, const uint16_t i2c_spi_addr_flags, const int reg,
	       int data);

/**
 * Read 16bit register from accelerometer.
 */
int bmi_read16(const int port, const uint16_t i2c_spi_addr_flags,
	       const uint8_t reg, int *data_ptr);

/**
 * Write 16bit register from accelerometer.
 */
int bmi_write16(const int port, const uint16_t i2c_spi_addr_flags,
		const int reg, int data);

/**
 * Read 32bit register from accelerometer.
 */
int bmi_read32(const int port, const uint16_t i2c_spi_addr_flags,
	       const uint8_t reg, int *data_ptr);

/**
 * Read n bytes from accelerometer.
 */
int bmi_read_n(const int port, const uint16_t i2c_spi_addr_flags,
	       const uint8_t reg, uint8_t *data_ptr, const int len);

/**
 * Write n bytes from accelerometer.
 */
int bmi_write_n(const int port, const uint16_t i2c_spi_addr_flags,
		const uint8_t reg, const uint8_t *data_ptr, const int len);

/*
 * Enable/Disable specific bit set of a 8-bit reg.
 */
int bmi_enable_reg8(const struct motion_sensor_t *s, int reg, uint8_t bits,
		    int enable);

/*
 * Set specific bit set to certain value of a 8-bit reg.
 */
int bmi_set_reg8(const struct motion_sensor_t *s, int reg, uint8_t bits,
		 int mask);

/*
 * @s: base sensor.
 * @v: output vector.
 * @input: 6-bits array input.
 */
void bmi_normalize(const struct motion_sensor_t *s, intv3_t v, uint8_t *input);

/*
 * Decode the header from the fifo.
 * Return 0 if we need further processing.
 * Sensor mutex must be held during processing, to protect the fifos.
 *
 * @accel: base sensor
 * @hdr: the header to decode
 * @last_ts: the last timestamp of fifo interrupt.
 * @bp: current pointer in the buffer, updated when processing the header.
 * @ep: pointer to the end of the valid data in the buffer.
 */
int bmi_decode_header(struct motion_sensor_t *accel, enum fifo_header hdr,
		      uint32_t last_ts, uint8_t **bp, uint8_t *ep);
/**
 * Retrieve hardware FIFO from sensor,
 * - put data in Sensor Hub fifo.
 * - update sensor raw_xyz vector with the last information.
 * We put raw data in hub fifo and process data from there.
 * @s: Pointer to sensor data.
 * @last_ts: The last timestamp of fifo interrupt.
 *
 * Read only up to  bmi_buffer. If more reads are needed, we will be called
 * again by the interrupt routine.
 *
 * NOTE: If a new driver supports this function, be sure to add a check
 * for spoof_mode in order to load the sensor stack with the spoofed
 * data.  See accelgyro_bmi260.c::load_fifo for an example.
 */
int bmi_load_fifo(struct motion_sensor_t *s, uint32_t last_ts);

int bmi_set_range(struct motion_sensor_t *s, int range, int rnd);

int bmi_get_data_rate(const struct motion_sensor_t *s);

int bmi_get_offset(const struct motion_sensor_t *s, int16_t *offset,
		   int16_t *temp);

int bmi_get_resolution(const struct motion_sensor_t *s);

int bmi_get_rms_noise(const struct motion_sensor_t *accel,
		      int rms_noise_100hz_mg);

int bmi_set_scale(const struct motion_sensor_t *s, const uint16_t *scale,
		  int16_t temp);

int bmi_get_scale(const struct motion_sensor_t *s, uint16_t *scale,
		  int16_t *temp);

/* Start/Stop the FIFO collecting events */
int bmi_enable_fifo(const struct motion_sensor_t *s, int enable);

/* Read the xyz data of accel/gyro */
int bmi_read(const struct motion_sensor_t *s, intv3_t v);

/* Read temperature of sensor s */
int bmi_read_temp(const struct motion_sensor_t *s, int *temp_ptr);

/* Read temperature of sensor idx */
int bmi_get_sensor_temp(int idx, int *temp_ptr);

/*
 * Get the normalized rate according to input rate and input rnd
 * @rate: input rate
 * @rnd: round up
 * @normalized_rate_ptr: normalized rate pointer for output
 * @reg_val_ptr: pointer to the actual register value of normalized rate for
 *               output.
 */
int bmi_get_normalized_rate(const struct motion_sensor_t *s, int rate, int rnd,
			    int *normalized_rate_ptr, uint8_t *reg_val_ptr);

/* Get the accelerometer offset */
int bmi_accel_get_offset(const struct motion_sensor_t *accel, intv3_t v);

/* Get the gyroscope offset */
int bmi_gyro_get_offset(const struct motion_sensor_t *gyro, intv3_t v);

/* Set the accelerometer offset */
int bmi_set_accel_offset(const struct motion_sensor_t *accel, intv3_t v);

/* Set the gyroscope offset */
int bmi_set_gyro_offset(const struct motion_sensor_t *gyro, intv3_t v,
			int *val98_ptr);

int bmi_list_activities(const struct motion_sensor_t *s, uint32_t *enabled,
			uint32_t *disabled);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_ACCELGYRO_BMI_COMMON_H */
