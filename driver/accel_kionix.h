/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Kionix Accelerometer driver for Chrome EC */

#ifndef __CROS_EC_ACCEL_KIONIX_H
#define __CROS_EC_ACCEL_KIONIX_H

#include "common.h"
#include "driver/accel_kx022.h"
#include "driver/accel_kxcj9.h"

enum kionix_accel {
	KX022,
	KXCJ9,
	SUPPORTED_KIONIX_ACCELS,
};

/*
 * Struct for pairing an engineering value with the register value for a
 * parameter.
 */
struct accel_param_pair {
	int val; /* Value in engineering units. */
	int reg; /* Corresponding register value. */
};

struct kionix_accel_data {
	/* Variant of Kionix Accelerometer. */
	uint8_t variant;
	/* Note, the following are indicies into their respective tables. */
	/* Current range of accelerometer. */
	int sensor_range;
	/* Current output data rate of accelerometer. */
	int sensor_datarate;
	/* Current resolution of accelerometer. */
	int sensor_resolution;
	/* Device address. */
	int accel_addr;
	int16_t offset[3];
};

extern const struct accelgyro_drv kionix_accel_drv;

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

#endif /* __CROS_EC_ACCEL_KIONIX_H */
