/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Kionix Accelerometer driver for Chrome EC */

#ifndef __CROS_EC_ACCEL_KIONIX_H
#define __CROS_EC_ACCEL_KIONIX_H

#include "common.h"
#include "accelgyro.h"
#include "driver/accel_kx022.h"
#include "driver/accel_kxcj9.h"

/*
 * Struct for pairing an engineering value with the register value for a
 * parameter.
 */
struct accel_param_pair {
	int val; /* Value in engineering units. */
	int reg; /* Corresponding register value. */
};

struct kionix_accel_data {
	struct accelgyro_saved_data_t base;
	/* Current resolution of accelerometer. */
	int sensor_resolution;
	int16_t offset[3];
#ifdef CONFIG_KX022_ORIENTATION_SENSOR
	int8_t raw_orientation;
	enum motionsensor_orientation orientation;
	enum motionsensor_orientation last_orientation;
#endif
};

extern const struct accelgyro_drv kionix_accel_drv;

/*
 * The addr field of motion_sensor support both SPI and I2C:
 *
 * +-------------------------------+---+
 * |    7 bit i2c address          | 0 |
 * +-------------------------------+---+
 * Or
 * +-------------------------------+---+
 * |    SPI device ID              | 1 |
 * +-------------------------------+---+
 */
#define KIONIX_CTRL1_REG(v) (KX022_CNTL1 +	\
			     (v) * (KXCJ9_CTRL1 - KX022_CNTL1))
#define KIONIX_CTRL2_REG(v) (KX022_CNTL2 +	\
			     (v) * (KXCJ9_CTRL2 - KX022_CNTL2))
#define KIONIX_ODR_REG(v) (KX022_ODCNTL +	\
			   (v) * (KXCJ9_DATA_CTRL - KX022_ODCNTL))
#define KIONIX_ODR_FIELD(v) (KX022_OSA_FIELD +		\
			     (v) * (KXCJ9_OSA_FIELD - KX022_OSA_FIELD))
#define KIONIX_PC1_FIELD(v) (KX022_CNTL1_PC1 +		\
			     (v) * (KXCJ9_CTRL1_PC1 - KX022_CNTL1_PC1))
#define KIONIX_RANGE_FIELD(v) (KX022_GSEL_FIELD +	\
			       (v) * (KXCJ9_GSEL_ALL - KX022_GSEL_FIELD))
#define KIONIX_RES_FIELD(v) (KX022_RES_16BIT +		\
			     (v) * (KXCJ9_RES_12BIT - KX022_RES_16BIT))
#define KIONIX_RESET_FIELD(v) (KX022_CNTL2_SRST +	\
			       (v) * (KXCJ9_CTRL2_SRST - KX022_CNTL2_SRST))
#define KIONIX_XOUT_L(v) (KX022_XOUT_L +	\
			  (v) * (KXCJ9_XOUT_L - KX022_XOUT_L))

#define KIONIX_WHO_AM_I(v) (KX022_WHOAMI + \
			    (v) * (KXCJ9_WHOAMI - KX022_WHOAMI))

#define KIONIX_WHO_AM_I_VAL(v) (KX022_WHO_AM_I_VAL + \
			(v) * (KXCJ9_WHO_AM_I_VAL - KX022_WHO_AM_I_VAL))

#ifdef CONFIG_CMD_I2C_STRESS_TEST_ACCEL
extern struct i2c_stress_test_dev kionix_i2c_stress_test_dev;
#endif

#ifdef CONFIG_KX022_ORIENTATION_SENSOR
#define ORIENTATION_CHANGED(_sensor) \
	(((struct kionix_accel_data *)(_sensor->drv_data))->orientation != \
	((struct kionix_accel_data *)(_sensor->drv_data))->last_orientation)

#define GET_ORIENTATION(_sensor) \
	(((struct kionix_accel_data *)(_sensor->drv_data))->orientation)

#define SET_ORIENTATION(_sensor, _val) \
	(((struct kionix_accel_data *)(_sensor->drv_data))->orientation = _val)

#define SET_ORIENTATION_UPDATED(_sensor) \
	(((struct kionix_accel_data *)(_sensor->drv_data))->last_orientation = \
	((struct kionix_accel_data *)(_sensor->drv_data))->orientation)
#endif

#endif /* __CROS_EC_ACCEL_KIONIX_H */
