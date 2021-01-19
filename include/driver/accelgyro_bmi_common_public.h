/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* BMI accelerometer and gyro common definitions for Chrome EC */

#ifndef __CROS_EC_DRIVER_ACCELGYRO_BMI_COMMON_PUBLIC_H
#define __CROS_EC_DRIVER_ACCELGYRO_BMI_COMMON_PUBLIC_H

/* Min and Max sampling frequency in mHz */
#define BMI_ACCEL_MIN_FREQ 12500
#define BMI_ACCEL_MAX_FREQ MOTION_MAX_SENSOR_FREQUENCY(1600000, 100000)
#define BMI_GYRO_MIN_FREQ  25000
#define BMI_GYRO_MAX_FREQ MOTION_MAX_SENSOR_FREQUENCY(3200000, 100000)

struct bmi_drv_data_t {
	struct accelgyro_saved_data_t saved_data[3];
	uint8_t              flags;
	uint8_t              enabled_activities;
	uint8_t              disabled_activities;
#ifdef CONFIG_MAG_BMI_BMM150
	struct bmm150_private_data compass;
#endif
#ifdef CONFIG_BMI_ORIENTATION_SENSOR
	uint8_t raw_orientation;
	enum motionsensor_orientation orientation;
	enum motionsensor_orientation last_orientation;
#endif

};

#endif /* __CROS_EC_DRIVER_ACCELGYRO_BMI_COMMON_PUBLIC_H */
