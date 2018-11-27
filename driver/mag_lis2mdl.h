/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LIS2MSL magnetometer module for Chrome EC */

#ifndef __CROS_EC_MAG_LIS2MDL_H
#define __CROS_EC_MAG_LIS2MDL_H

#define LIS2MDL_I2C_ADDR(__x)		(__x << 1)

#define LIS2MDL_ADDR0			LIS2MDL_I2C_ADDR(0x1e)
#define LIS2MDL_ADDR1			LIS2MDL_I2C_ADDR(0x1f)

#define LIS2MDL_WHO_AM_I_REG		0x4f
#define LIS2MDL_WHO_AM_I		0x40

#define LIS2MDL_CFG_REG_A_ADDR		0x60
#define LIS2MDL_SW_RESET		0x20
#define LIS2MDL_ODR_100HZ		0xc
#define LIS2MDL_CONT_MODE		0x0

#define LIS2MDL_STATUS_REG		0x67
#define LIS2MDL_OUT_REG			0x68

#define LIS2MDL_RANGE			4915
#define LIS2MDL_RESOLUTION		16

#define LIS2MDL_ODR_MIN_VAL	10000
#define LIS2MDL_ODR_MAX_VAL	100000
#if (CONFIG_EC_MAX_SENSOR_FREQ_MILLIHZ <= LIS2MDL_ODR_MAX_VAL)
#error "EC too slow for magnetometer"
#endif

extern const struct accelgyro_drv lis2mdl_drv;

#endif /* __CROS_EC_MAG_LIS2MDL_H */

