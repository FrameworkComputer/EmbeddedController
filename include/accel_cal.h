/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Online accelerometer calibration */

#ifndef __CROS_EC_ACCEL_CAL_H
#define __CROS_EC_ACCEL_CAL_H

#include "common.h"
#include "kasa.h"
#include "newton_fit.h"
#include "stdbool.h"
#include "stillness_detector.h"

#ifdef __cplusplus
extern "C" {
#endif

struct accel_cal_algo {
	struct kasa_fit kasa_fit;
	struct newton_fit newton_fit;
};

struct accel_cal {
	struct still_det still_det;
	struct accel_cal_algo *algos;
	uint8_t num_temp_windows;
	fpv3_t bias;
};

/**
 * Reset the accelerometer calibration object. This should only be called
 * once. The struct will reset automatically in accel_cal_accumulate when
 * a new calibration is computed.
 *
 * @param cal Pointer to the accel_cal struct to reset.
 */
void accel_cal_reset(struct accel_cal *cal);

/**
 * Add new reading to the accelerometer calibration.
 *
 * @param cal Pointer to the accel_cal struct to update.
 * @param sample_time The timestamp when the sample was taken.
 * @param x X component of the new reading.
 * @param y Y component of the new reading.
 * @param z Z component of the new reading.
 * @param temp The sensor's internal temperature in degrees C.
 * @return True if a new bias is available.
 */
bool accel_cal_accumulate(struct accel_cal *cal, uint32_t sample_time, fp_t x,
			  fp_t y, fp_t z, fp_t temp);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_ACCEL_CAL_H */
