/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ICM accelerometer and gyroscope common definitions for Chrome EC */

#ifndef __CROS_EC_ACCELGYRO_ICM_COMMON_H
#define __CROS_EC_ACCELGYRO_ICM_COMMON_H

#include "accelgyro.h"
#include "builtin/stddef.h"
#include "hwtimer.h"
#include "timer.h"

#include <sys/types.h>

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 23

#if !defined(CONFIG_ACCELGYRO_ICM_COMM_SPI) && \
	!defined(CONFIG_ACCELGYRO_ICM_COMM_I2C)
#error "ICM must use either SPI or I2C communication"
#endif

#ifdef CONFIG_ACCEL_FIFO
/* reserve maximum 4 samples of 16 bytes */
#define ICM_FIFO_BUFFER 64
#else
#define ICM_FIFO_BUFFER 0
#endif

struct icm_drv_data_t {
	struct accelgyro_saved_data_t saved_data[2];
	struct motion_sensor_t *accel;
	struct motion_sensor_t *gyro;
	uint32_t stabilize_ts[2];
	uint8_t bank;
	uint8_t fifo_en;
	uint8_t fifo_buffer[ICM_FIFO_BUFFER] __aligned(sizeof(long));
};

#define ICM_GET_DATA(_s) ((struct icm_drv_data_t *)(_s)->drv_data)
#define ICM_GET_SAVED_DATA(_s) (&ICM_GET_DATA(_s)->saved_data[(_s)->type])

/*
 * Virtual register address is 16 bits:
 * - 8 bits MSB coding bank number
 * - 8 bits LSB coding physical address
 */
#define ICM426XX_REG_GET_BANK(_r) (((_r) & 0xFF00) >> 8)
#define ICM426XX_REG_GET_ADDR(_r) ((_r) & 0x00FF)

/* Sensor resolution in number of bits */
#define ICM_RESOLUTION 16

/**
 * icm_sign_extend - sign extend a standard int value using the given sign-bit
 * @value: value to sign extend
 * @index: 0 based bit index to sign bit
 */
static inline int icm_sign_extend(int value, int index)
{
	int shift = (sizeof(int) * 8) - index - 1;

	return (int)(value << shift) >> shift;
}

/**
 * Read 8 bits register
 */
int icm_read8(const struct motion_sensor_t *s, const int reg, int *data_ptr);

/**
 * Write 8 bits register
 */
int icm_write8(const struct motion_sensor_t *s, const int reg, int data);

/**
 * Read 16 bits register
 */
int icm_read16(const struct motion_sensor_t *s, const int reg, int *data_ptr);

/**
 * Write 16 bits register
 */
int icm_write16(const struct motion_sensor_t *s, const int reg, int data);

/**
 * Read n bytes
 */
int icm_read_n(const struct motion_sensor_t *s, const int reg,
	       uint8_t *data_ptr, const int len);

int icm_field_update8(const struct motion_sensor_t *s, const int reg,
		      const uint8_t field_mask, const uint8_t set_value);

int icm_get_resolution(const struct motion_sensor_t *s);

int icm_get_range(const struct motion_sensor_t *s);

int icm_get_data_rate(const struct motion_sensor_t *s);

int icm_set_scale(const struct motion_sensor_t *s, const uint16_t *scale,
		  int16_t temp);

int icm_get_scale(const struct motion_sensor_t *s, uint16_t *scale,
		  int16_t *temp);

ssize_t icm_fifo_decode_packet(const void *packet, const uint8_t **accel,
			       const uint8_t **gyro);

static inline void icm_set_stabilize_ts(const struct motion_sensor_t *s,
					uint32_t delay)
{
	struct icm_drv_data_t *st = ICM_GET_DATA(s);
	uint32_t stabilize_ts;

	stabilize_ts = __hw_clock_source_read() + delay;
	/* prevent 0 value used for disabling time checking */
	st->stabilize_ts[s->type] = stabilize_ts | 1;
}

static inline void icm_reset_stabilize_ts(const struct motion_sensor_t *s)
{
	struct icm_drv_data_t *st = ICM_GET_DATA(s);

	st->stabilize_ts[s->type] = 0;
}

static inline int32_t icm_get_sensor_stabilized(const struct motion_sensor_t *s,
						uint32_t ts)
{
	struct icm_drv_data_t *st = ICM_GET_DATA(s);
	uint32_t stabilize_ts = st->stabilize_ts[s->type];

	if (stabilize_ts == 0)
		return 0;

	return time_until(ts, stabilize_ts);
}

#endif /* __CROS_EC_ACCELGYRO_ICM_COMMON_H */
