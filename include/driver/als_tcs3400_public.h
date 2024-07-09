/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * AMS TCS3400 light sensor driver
 */

#ifndef __CROS_EC_DRIVER_ALS_TCS3400_PUBLIC_H
#define __CROS_EC_DRIVER_ALS_TCS3400_PUBLIC_H

#include "accelgyro.h"

#ifdef __cplusplus
extern "C" {
#endif

/* I2C Interface */
#define TCS3400_I2C_ADDR_FLAGS 0x39

/* NOTE: The higher the ATIME value in reg, the shorter the accumulation time */
#define TCS_MIN_ATIME 0x00 /* 712 ms */
#define TCS_MAX_ATIME 0x70 /* 400 ms */
#define TCS_ATIME_GRANULARITY 256 /* 256 atime settings */
#define TCS_SATURATION_LEVEL 0xffff /* for 0 < atime < 0x70 */
#define TCS_DEFAULT_ATIME TCS_MIN_ATIME /* 712 ms */
#define TCS_CALIBRATION_ATIME TCS_MIN_ATIME
#define TCS_GAIN_UPSHIFT_ATIME TCS_MAX_ATIME

/* Number of different ranges supported for atime adjustment support */
#define TCS_MAX_ATIME_RANGES 13
#define TCS_GAIN_TABLE_MAX_LUX 12999
#define TCS_ATIME_GAIN_FACTOR 100 /* table values are 100x actual value */

#define TCS_MIN_AGAIN 0x00 /* 1x gain */
#define TCS_MAX_AGAIN 0x03 /* 64x gain */
#define TCS_CALIBRATION_AGAIN 0x02 /* 16x gain */
#define TCS_DEFAULT_AGAIN TCS_CALIBRATION_AGAIN

#define TCS_MAX_INTEGRATION_TIME 2780 /* 2780us */
#define TCS_ATIME_DEC_STEP 5
#define TCS_ATIME_INC_STEP TCS_GAIN_UPSHIFT_ATIME

/* Min and Max sampling frequency in mHz */
#define TCS3400_LIGHT_MIN_FREQ 149
#define TCS3400_LIGHT_MAX_FREQ 1000
#if (CONFIG_EC_MAX_SENSOR_FREQ_MILLIHZ <= TCS3400_LIGHT_MAX_FREQ)
#error "EC too slow for light sensor"
#endif

/* saturation auto-adjustment */
struct tcs_saturation_t {
	/*
	 * Gain Scaling; must be value between 0 and 3
	 *      0 - 1x scaling
	 *      1 - 4x scaling
	 *      2 - 16x scaling
	 *      3 - 64x scaling
	 */
	uint8_t again;

	/* Acquisition Time, controlled by the ATIME register */
	uint8_t atime; /* ATIME register setting */
};

/* tcs3400 rgb als driver data */
struct tcs3400_rgb_drv_data_t {
	uint8_t calibration_mode; /* 0 = normal run mode, 1 = calibration mode
				   */

	struct rgb_calibration_t calibration;
	struct tcs_saturation_t saturation; /* saturation adjustment */
};

extern const struct accelgyro_drv tcs3400_drv;
extern const struct accelgyro_drv tcs3400_rgb_drv;

void tcs3400_interrupt(enum gpio_signal signal);
int tcs3400_get_integration_time(int atime);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_DRIVER_ALS_TCS3400_PUBLIC_H */
