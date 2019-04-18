/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Common test code to test lid angle calculation.
 */
#ifndef __CROS_EC_MOTION_COMMON_H
#define __CROS_EC_MOTION_COMMON_H

#include "motion_sense.h"
/*
 * Period in us for the motion task period.
 * The task will read the vectors at that interval
 */
#define TEST_LID_EC_RATE (1 * MSEC)
#define TEST_LID_FREQUENCY (1e9 / TEST_LID_EC_RATE)   /* mHz */

/*
 * Time in ms to wait for the task to read the vectors.
 */
#define TEST_LID_SLEEP_RATE (TEST_LID_EC_RATE / 5)

/* We gather 6 elements [2 vectors of 3 axis] per sample. */
#define TEST_LID_SAMPLE_SIZE (2 * 3)

extern enum chipset_state_mask sensor_active;

extern struct motion_sensor_t motion_sensors[];
extern const unsigned int motion_sensor_count;

void wait_for_valid_sample(void);
void feed_accel_data(const float *array, int *idx,
		int (filler)(const struct motion_sensor_t *s, const float f));

/*
 * External data - from
 * chromium/src/ash/wm/tablet_mode/tablet_mode_controller_unittest.cc
 *
 * Test accelerometer data taken with the lid at less than 180 degrees while
 * shaking the device around. The data is to be interpreted in groups of 6 where
 * each 6 values corresponds to the base accelerometer (-y / g, -x / g, -z / g)
 * followed by the lid accelerometer (-y / g , x / g, z / g).
 * [ CONFIG_ACCEL_STD_REF_FRAME_OLD must be defined to used this array. ]
 */
extern const float kAccelerometerLaptopModeTestData[];
extern const size_t kAccelerometerLaptopModeTestDataLength;

/*
 * Test accelerometer data taken with the lid open 360 degrees while
 * shaking the device around. The data is to be interpreted in groups of 6 where
 * each 6 values corresponds to the base accelerometer (-y / g, -x / g, -z / g)
 * followed by the lid accelerometer (-y / g , x / g, z / g).
 * [ CONFIG_ACCEL_STD_REF_FRAME_OLD must be defined to used this array. ]
 */
extern const float kAccelerometerFullyOpenTestData[];
extern const size_t kAccelerometerFullyOpenTestDataLength;

/*
 * Test accelerometer data taken with the lid open 360 degrees while the device
 * hinge was nearly vertical, while shaking the device around. The data is to be
 * interpreted in groups of 6 where each 6 values corresponds to the X, Y, and Z
 * readings from the base and lid accelerometers in this order.
 */
extern const float kAccelerometerVerticalHingeTestData[];
extern const size_t kAccelerometerVerticalHingeTestDataLength;
extern const float kAccelerometerVerticalHingeUnstableTestData[];
extern const size_t kAccelerometerVerticalHingeUnstableTestDataLength;
#endif  /* __CROS_EC_MOTION_COMMON_H */
