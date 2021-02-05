/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Sensor configuration on Volteer board */

#ifndef __ZEPHYR_SENSOR_MAP_H
#define __ZEPHYR_SENSOR_MAP_H

/*
 * TODO(b/173507858)
 * For now, this file is used to define missing motionsense related CONFIG_xxx.
 * Once we have all CONFIG_xxx in Kconfig and move all board specific things to
 * .dts then we will remove this file.
 */

/*
 * TODO(b/173507858) : Everything below will be moved to device tree
*/

enum sensor_id {
	LID_ACCEL = 0,
	BASE_ACCEL,
	BASE_GYRO,
	CLEAR_ALS,
	RGB_ALS,
	SENSOR_COUNT,
};

#ifdef CONFIG_ALS_TCS3400
#define CONFIG_ALS_TCS3400_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(CLEAR_ALS)
#endif

#ifdef CONFIG_ACCELGYRO_BMI260
#define CONFIG_ACCELGYRO_BMI260_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
#endif

#ifdef CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_SENSOR_BASE            BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID             LID_ACCEL
#endif

/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK    (BIT(LID_ACCEL) | BIT(CLEAR_ALS))

#endif /* __ZEPHYR_SENSOR_MAP_H */
