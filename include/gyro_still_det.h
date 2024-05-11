/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_GYRO_STILL_DET_H
#define __CROS_EC_GYRO_STILL_DET_H

#include "common.h"
#include "math_util.h"
#include "stdbool.h"
#include "vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

struct gyro_still_det {
	/**
	 * Variance threshold for the stillness confidence score.
	 * [sensor units]^2
	 */
	fp_t var_threshold;

	/**
	 * Delta about the variance threshold for calculation of the stillness
	 * confidence score [0,1]. [sensor units]^2
	 */
	fp_t confidence_delta;

	/**
	 * Flag to indicate when enough samples have been collected for
	 * a complete stillness calculation.
	 */
	bool stillness_window_ready;

	/**
	 * Flag to signal the beginning of a new stillness detection window.
	 * This is used to keep track of the window start time.
	 */
	bool start_new_window;

	/** Starting time stamp for the current window. */
	uint32_t window_start_time;

	/**
	 * Accumulator variables for tracking the sample mean during
	 * the stillness period.
	 */
	uint32_t num_acc_samples;
	fpv3_t mean;

	/**
	 * Accumulator variables for computing the window sample mean and
	 * variance for the current window (used for stillness detection).
	 */
	uint32_t num_acc_win_samples;
	fpv3_t win_mean;
	fpv3_t assumed_mean;
	fpv3_t acc_var;

	/** Stillness period mean (used for look-ahead). */
	fpv3_t prev_mean;

	/** Latest computed variance. */
	fpv3_t win_var;

	/**
	 * Stillness confidence score for current and previous sample
	 * windows [0,1] (used for look-ahead).
	 */
	fp_t stillness_confidence;
	fp_t prev_stillness_confidence;

	/** Timestamp of last sample recorded. */
	uint32_t last_sample_time;
};

/** Update the stillness detector with a new sample. */
void gyro_still_det_update(struct gyro_still_det *gyro_still_det,
			   uint32_t stillness_win_endtime, uint32_t sample_time,
			   fp_t x, fp_t y, fp_t z);

/** Calculates and returns the stillness confidence score [0,1]. */
fp_t gyro_still_det_compute(struct gyro_still_det *gyro_still_det);

/**
 * Resets the stillness detector and initiates a new detection window.
 *
 * @param reset_stats Determines whether the stillness statistics are reset.
 */
void gyro_still_det_reset(struct gyro_still_det *gyro_still_det,
			  bool reset_stats);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_GYRO_STILL_DET_H */
