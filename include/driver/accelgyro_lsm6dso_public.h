/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LSM6DSO Accel and Gyro driver for Chrome EC */

#ifndef __CROS_EC_ACCELGYRO_LSM6DSO_PUBLIC_H
#define __CROS_EC_ACCELGYRO_LSM6DSO_PUBLIC_H

/*
 * 7-bit address is 110101xb. Where 'x' is determined
 * by the voltage on the ADDR pin
 */
#define LSM6DSO_ADDR0_FLAGS 0x6a
#define LSM6DSO_ADDR1_FLAGS 0x6b

/* Absolute maximum rate for Acc and Gyro sensors */
#define LSM6DSO_ODR_MIN_VAL 13000
#define LSM6DSO_ODR_MAX_VAL MOTION_MAX_SENSOR_FREQUENCY(416000, 13000)

/* Who Am I */
#define LSM6DSO_WHO_AM_I_REG 0x0f
#define LSM6DSO_WHO_AM_I 0x6c

/* INT1 control register */
#define LSM6DSO_INT1_CTRL 0x0d

/* CTRL3 register */
#define LSM6DSO_CTRL3_ADDR 0x12
#define LSM6DSO_SW_RESET 0x01
#define LSM6DSO_IF_INC 0x04
#define LSM6DSO_PP_OD 0x10
#define LSM6DSO_H_L_ACTIVE 0x20
#define LSM6DSO_BDU 0x40

#endif /* __CROS_EC_ACCELGYRO_LSM6DSO_PUBLIC_H */
