/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LSM6DS0 accelerometer and gyro module for Chrome EC */

#ifndef __CROS_EC_ACCEL_LSM6DS0_H
#define __CROS_EC_ACCEL_LSM6DS0_H

/*
 * 7-bit address is 110101Xb. Where 'X' is determined
 * by the voltage on the ADDR pin.
 */
#define LSM6DS0_ADDR0             0xd4
#define LSM6DS0_ADDR1             0xd6

/* Chip specific registers. */
#define LSM6DS0_CTRL_REG6_XL      0x20
#define LSM6DS0_CTRL_REG8         0x22
#define LSM6DS0_OUT_X_L_XL        0x28
#define LSM6DS0_OUT_X_H_XL        0x29
#define LSM6DS0_OUT_Y_L_XL        0x2a
#define LSM6DS0_OUT_Y_H_XL        0x2b
#define LSM6DS0_OUT_Z_L_XL        0x2c
#define LSM6DS0_OUT_Z_H_XL        0x2d


#define LSM6DS0_GSEL_2G         (0 << 3)
#define LSM6DS0_GSEL_4G         (2 << 3)
#define LSM6DS0_GSEL_8G         (3 << 3)
#define LSM6DS0_GSEL_ALL        (3 << 3)

#define LSM6DS0_ODR_10HZ        (1 << 5)
#define LSM6DS0_ODR_50HZ        (2 << 5)
#define LSM6DS0_ODR_119HZ       (3 << 5)
#define LSM6DS0_ODR_238HZ       (4 << 5)
#define LSM6DS0_ODR_476HZ       (5 << 5)
#define LSM6DS0_ODR_982HZ       (6 << 5)
#define LSM6DS0_ODR_ALL         (7 << 5)

/* Sensor resolution in number of bits. This sensor has fixed resolution. */
#define LSM6DS0_RESOLUTION      16

#endif /* __CROS_EC_ACCEL_LSM6DS0_H */
