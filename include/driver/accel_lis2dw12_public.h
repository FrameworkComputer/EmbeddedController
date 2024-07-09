/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LIS2DW12 gsensor module for Chrome EC */

#ifndef __CROS_EC_DRIVER_ACCEL_LIS2DW12_PUBLIC_H
#define __CROS_EC_DRIVER_ACCEL_LIS2DW12_PUBLIC_H

#include "config.h"
#include "gpio_signal.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const struct accelgyro_drv lis2dw12_drv;

/* I2C ADDRESS DEFINITIONS
 *
 * 7-bit address is 011000Xb. Where 'X' is determined
 * by the voltage on the ADDR pin.
 */
#define LIS2DW12_ADDR0 0x18
#define LIS2DW12_ADDR1 0x19

#define LIS2DWL_ADDR0_FLAGS 0x18
#define LIS2DWL_ADDR1_FLAGS 0x19

#define LIS2DW12_EN_BIT 0x01
#define LIS2DW12_DIS_BIT 0x00

/* Absolute Acc rate. */
#define LIS2DW12_ODR_MIN_VAL 12500
#define LIS2DW12_ODR_MAX_VAL \
	MOTION_MAX_SENSOR_FREQUENCY(1600000, LIS2DW12_ODR_MIN_VAL)

void lis2dw12_interrupt(enum gpio_signal signal);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_DRIVER_ACCEL_LIS2DW12_PUBLIC_H */
