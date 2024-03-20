/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Vishay VEML3328 light sensor driver
 */

#ifndef __CROS_EC_ALS_VEML3328_H
#define __CROS_EC_ALS_VEML3328_H

#include "accelgyro.h"

/* I2C interface */
#define VEML3328_I2C_ADDR 0x10

/* Register definition */
#define VEML3328_REG_CONF 0x00
#define VEML3328_REG_C 0x04
#define VEML3328_REG_R 0x05
#define VEML3328_REG_G 0x06
#define VEML3328_REG_B 0x07
#define VEML3328_REG_IR 0x08
#define VEML3328_REG_ID 0x0C

/* Register value definition : CONF */
#define VEML3328_SD 0x8001
#define VEML3328_IT_MASK 0x0030
#define VEML3328_IT_50MS 0x0000
#define VEML3328_IT_100MS 0x0010
#define VEML3328_IT_200MS 0x0020
#define VEML3328_IT_400MS 0x0030
#define VEML3328_IT_SHIFT 4
#define VEML3328_HD_MASK 0x0040
#define VEML3328_HD_X1 0x0000
#define VEML3328_HD_X1_3 0x0040
#define VEML3328_HD_SHIFT 6
#define VEML3328_GAIN_MASK 0x0C00
#define VEML3328_GAIN_X1_2 0x0C00
#define VEML3328_GAIN_X1 0x0000
#define VEML3328_GAIN_X2 0x0400
#define VEML3328_GAIN_X4 0x0800
#define VEML3328_GAIN_SHIFT 10
#define VEML3328_DG_MASK 0x3000
#define VEML3328_DG_X1 0x0000
#define VEML3328_DG_X2 0x1000
#define VEML3328_DG_X4 0x2000
#define VEML3328_DG_SHIFT 12
#define VEML3328_SD_R_B 0x4000

#define VEML3328_CONF_DEFAULT \
	(VEML3328_IT_100MS | VEML3328_HD_X1 | VEML3328_GAIN_X1 | VEML3328_DG_X2)

/* Various gain coefficients
 * Integration time (IT) : 50ms, 100ms, 200ms, 400ms
 * DG			 : x1, x2, x4
 * GAIN			 : x1, x2, x4, x1/2
 * Sensitivity (SENS)	 : x1, x1/3
 *
 * Default values are IT * DG * GAIN * SENS
 *                    1  * 2  *  1   * 1
 */
#define VEML3328_DEFAULT_GAIN (2)

/* Register ID_L value */
#define VEML3328_DEV_ID_MASK 0xff
#define VEML3328_DEV_ID 0x28

/* Range from 1 to 5 Hz */
#define VEML3328_MAX_FREQ (5 * 1000)
#define VEML3328_MIN_FREQ (1 * 1000)

struct veml3328_calib {
	/* Lux */
	float LG;
	/* xy */
	float A0, A1, A2;
	float B0, B1, B2;
	float Dx_min, Dx_max;
	float Dy_min, Dy_max;
};

struct veml3328_rgb_drv_data_t {
	int calibration_mode;
	struct rgb_calibration_t calibration;
	struct veml3328_calib calib;
};

extern const struct accelgyro_drv veml3328_drv;
extern const struct accelgyro_drv veml3328_rgb_drv;

#endif /* __CROS_EC_ALS_VEML3328_H */
