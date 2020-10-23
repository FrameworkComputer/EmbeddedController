/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_GYRO_CAL_INIT_FOR_TEST
#define __CROS_EC_GYRO_CAL_INIT_FOR_TEST

#include "gyro_cal.h"
#include "gyro_still_det.h"

/**
 * Initialization function used for testing the gyroscope calibration.
 * This function will initialize to the following values:
 * - Gyrscope stillness detector
 *   - variance threshold:		5e-5
 *   - confidence delta:		1e-5
 * - Accelerometer stillness detector
 *   - variance threshold:		8e-3
 *   - confidence delta:		1.6e-3
 * - Magnetometer stillness detector
 *   - variance threshold:		1.4
 *   - confidence delta:		2.5e-1
 * - Minimum stillness duration:	5 seconds
 * - Maximum stillness duration:	6 seconds
 * - Window duration:			1.5 seconds
 * - Window timeout duration:		5 seconds
 * - Stillness threshold:		0.95
 * - Stillness mean delta limit:	50 millidegrees
 * - Temperature delta limit:		1.5K
 *
 * Once all the values are set, this function will call init_gyro_cal()
 * to finish initializing/resetting the struct data.
 *
 * @param gyro_cal Pointer to the calibration data structure to initialize.
 */
void gyro_cal_initialization_for_test(struct gyro_cal *gyro_cal);

#endif /* __CROS_EC_GYRO_CAL_INIT_FOR_TEST */
