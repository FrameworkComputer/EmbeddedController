/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "stillness_detector.h"
#include "timer.h"
#include <string.h>

static void still_det_reset(struct still_det *still_det)
{
	still_det->num_samples = 0;
	still_det->acc_x = FLOAT_TO_FP(0.0f);
	still_det->acc_y = FLOAT_TO_FP(0.0f);
	still_det->acc_z = FLOAT_TO_FP(0.0f);
	still_det->acc_xx = FLOAT_TO_FP(0.0f);
	still_det->acc_yy = FLOAT_TO_FP(0.0f);
	still_det->acc_zz = FLOAT_TO_FP(0.0f);
}

static bool stillness_batch_complete(struct still_det *still_det,
				     uint32_t sample_time)
{
	bool complete = false;
	uint32_t batch_window = time_until(still_det->window_start_time,
					   sample_time);

	/* Checking if enough data is accumulated */
	if (batch_window >= still_det->min_batch_window &&
	    still_det->num_samples > still_det->min_batch_size) {
		if (batch_window <= still_det->max_batch_window) {
			complete = true;
		} else {
			/* Checking for too long batch window, reset and start
			 * over
			 */
			still_det_reset(still_det);
		}
	} else if (batch_window > still_det->min_batch_window &&
		   still_det->num_samples < still_det->min_batch_size) {
		/* Not enough samples collected, reset and start over */
		still_det_reset(still_det);
	}
	return complete;
}

static inline fp_t compute_variance(fp_t acc_squared, fp_t acc, fp_t inv)
{
	/* (acc^2 - (acc * acc * inv)) * inv */
	return fp_mul((acc_squared - fp_mul(fp_sq(acc), inv)), inv);
}

bool still_det_update(struct still_det *still_det, uint32_t sample_time,
		      fp_t x, fp_t y, fp_t z)
{
	fp_t inv = FLOAT_TO_FP(0.0f), var_x, var_y, var_z;
	bool complete = false;

	/* Accumulate for mean and VAR */
	still_det->acc_x += x;
	still_det->acc_y += y;
	still_det->acc_z += z;
	still_det->acc_xx += fp_mul(x, x);
	still_det->acc_yy += fp_mul(y, y);
	still_det->acc_zz += fp_mul(z, z);

	switch (++still_det->num_samples) {
	case 0:
		/* If we rolled over, go back. */
		still_det->num_samples--;
		break;
	case 1:
		/* Set a new start time if new batch. */
		still_det->window_start_time = sample_time;
		break;
	}

	if (stillness_batch_complete(still_det, sample_time)) {
		/*
		 * Compute 1/num_samples and check for num_samples == 0 (should
		 * never happen, but just in case)
		 */
		if (still_det->num_samples) {
			inv = fp_div(1.0f, INT_TO_FP(still_det->num_samples));
		} else {
			still_det_reset(still_det);
			return complete;
		}
		/* Calculating the VAR = sum(x^2)/n - sum(x)^2/n^2 */
		var_x = compute_variance(
			still_det->acc_xx, still_det->acc_x, inv);
		var_y = compute_variance(
			still_det->acc_yy, still_det->acc_y, inv);
		var_z = compute_variance(
			still_det->acc_zz, still_det->acc_z, inv);
		/* Checking if sensor is still */
		if (var_x < still_det->var_threshold &&
		    var_y < still_det->var_threshold &&
		    var_z < still_det->var_threshold) {
			still_det->mean_x = fp_mul(still_det->acc_x, inv);
			still_det->mean_y = fp_mul(still_det->acc_y, inv);
			still_det->mean_z = fp_mul(still_det->acc_z, inv);
			complete = true;
		}
		/* Reset and start over */
		still_det_reset(still_det);
	}
	return complete;
}
