/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "accel_cal.h"

#define CPRINTS(format, args...) cprints(CC_MOTION_SENSE, format, ##args)

#define TEMP_RANGE (CONFIG_ACCEL_CAL_MAX_TEMP - CONFIG_ACCEL_CAL_MIN_TEMP)

void accel_cal_reset(struct accel_cal *cal)
{
	int i;

	for (i = 0; i < cal->num_temp_windows; ++i) {
		kasa_reset(&(cal->algos[i].kasa_fit));
		newton_fit_reset(&(cal->algos[i].newton_fit));
	}
}

static inline int compute_temp_gate(const struct accel_cal *cal, fp_t temp)
{
	int gate = (int) fp_div(fp_mul(temp - CONFIG_ACCEL_CAL_MIN_TEMP,
				       INT_TO_FP(cal->num_temp_windows)),
				TEMP_RANGE);

	return gate < cal->num_temp_windows
		? gate : (cal->num_temp_windows - 1);
}

test_mockable bool accel_cal_accumulate(
	struct accel_cal *cal, uint32_t timestamp, fp_t x, fp_t y, fp_t z,
	fp_t temp)
{
	struct accel_cal_algo *algo;

	/* Test that we're within the temperature range. */
	if (temp >= CONFIG_ACCEL_CAL_MAX_TEMP ||
	    temp <= CONFIG_ACCEL_CAL_MIN_TEMP)
		return false;

	/* Test that we have a still sample. */
	if (!still_det_update(&cal->still_det, timestamp, x, y, z))
		return false;

	/* We have a still sample, update x, y, and z to the mean. */
	x = cal->still_det.mean_x;
	y = cal->still_det.mean_y;
	z = cal->still_det.mean_z;

	/* Compute the temp gate. */
	algo = &cal->algos[compute_temp_gate(cal, temp)];

	kasa_accumulate(&algo->kasa_fit, x, y, z);
	if (newton_fit_accumulate(&algo->newton_fit, x, y, z)) {
		fp_t radius;

		kasa_compute(&algo->kasa_fit, cal->bias, &radius);
		if (ABS(radius - FLOAT_TO_FP(1.0f)) <
		    CONFIG_ACCEL_CAL_KASA_RADIUS_THRES)
			goto accel_cal_accumulate_success;

		newton_fit_compute(&algo->newton_fit, cal->bias, &radius);
		if (ABS(radius - FLOAT_TO_FP(1.0f)) <
		    CONFIG_ACCEL_CAL_NEWTON_RADIUS_THRES)
			goto accel_cal_accumulate_success;
	}

	return false;

accel_cal_accumulate_success:
	accel_cal_reset(cal);

	return true;
}
