/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* BMA2x2 gsensor module for Chrome EC */

#ifndef __CROS_EC_DRIVER_ACCEL_BMA2x2_PUBLIC_H
#define __CROS_EC_DRIVER_ACCEL_BMA2x2_PUBLIC_H

#include "accelgyro.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const struct accelgyro_drv bma2x2_accel_drv;

/* I2C ADDRESS DEFINITIONS    */
/* The following definition of I2C address is used for the following sensors
 * BMA253
 * BMA255
 * BMA355
 * BMA280
 * BMA282
 * BMA223
 * BMA254
 * BMA284
 * BMA250E
 * BMA222E
 */
#define BMA2x2_I2C_ADDR1_FLAGS 0x18
#define BMA2x2_I2C_ADDR2_FLAGS 0x19

/* The following definition of I2C address is used for the following sensors
 * BMC150
 * BMC056
 * BMC156
 */
#define BMA2x2_I2C_ADDR3_FLAGS 0x10
#define BMA2x2_I2C_ADDR4_FLAGS 0x11

/*
 * Min and Max sampling frequency in mHz.
 * Given BMA255 is polled, we limit max frequency to 125Hz.
 * If set to 250Hz, given we can read up to 3ms before the due time
 * (see CONFIG_MOTION_MIN_SENSE_WAIT_TIME), we may read too early when
 * other sensors are active.
 */
#define BMA255_ACCEL_MIN_FREQ 7810
#define BMA255_ACCEL_MAX_FREQ MOTION_MAX_SENSOR_FREQUENCY(125000, 15625)

#ifdef __cplusplus
}
#endif

#endif /* CROS_EC_DRIVER_ACCEL_BMA2x2_PUBLIC_H */
