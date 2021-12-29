/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LSM6DSO Accel and Gyro driver for Chrome EC */

#ifndef __CROS_EC_ACCELGYRO_LSM6DSO_PUBLIC_H
#define __CROS_EC_ACCELGYRO_LSM6DSO_PUBLIC_H

#include "driver/accelgyro_lsm6dso_public.h"

/*
 * 7-bit address is 110101xb. Where 'x' is determined
 * by the voltage on the ADDR pin
 */
#define LSM6DSO_ADDR0_FLAGS		0x6a
#define LSM6DSO_ADDR1_FLAGS		0x6b

/* Absolute maximum rate for Acc and Gyro sensors */
#define LSM6DSO_ODR_MIN_VAL		13000
#define LSM6DSO_ODR_MAX_VAL \
	MOTION_MAX_SENSOR_FREQUENCY(416000, 13000)

#endif /* __CROS_EC_ACCELGYRO_LSM6DSO_PUBLIC_H */
