/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gyro_still_det.h"
#include "vec3.h"

/* Enforces the limits of an input value [0,1]. */
static fp_t gyro_still_det_limit(fp_t value);

void gyro_still_det_update(struct gyro_still_det *gyro_still_det,
			   uint32_t stillness_win_endtime, uint32_t sample_time,
			   fp_t x, fp_t y, fp_t z)
{
	fp_t delta = INT_TO_FP(0);

	/*
	 * Using the method of the assumed mean to preserve some numerical
	 * stability while avoiding per-sample divisions that the more
	 * numerically stable Welford method would afford.
	 *
	 * Reference for the numerical method used below to compute the
	 * online mean and variance statistics:
	 *   1). en.wikipedia.org/wiki/assumed_mean
	 */

	/* Increment the number of samples. */
	gyro_still_det->num_acc_samples++;

	/* Online computation of mean for the running stillness period. */
	gyro_still_det->mean[X] += x;
	gyro_still_det->mean[Y] += y;
	gyro_still_det->mean[Z] += z;

	/* Is this the first sample of a new window? */
	if (gyro_still_det->start_new_window) {
		/* Record the window start time. */
		gyro_still_det->window_start_time = sample_time;
		gyro_still_det->start_new_window = false;

		/* Update assumed mean values. */
		gyro_still_det->assumed_mean[X] = x;
		gyro_still_det->assumed_mean[Y] = y;
		gyro_still_det->assumed_mean[Z] = z;

		/* Reset current window mean and variance. */
		gyro_still_det->num_acc_win_samples = 0;
		gyro_still_det->win_mean[X] = INT_TO_FP(0);
		gyro_still_det->win_mean[Y] = INT_TO_FP(0);
		gyro_still_det->win_mean[Z] = INT_TO_FP(0);
		gyro_still_det->acc_var[X] = INT_TO_FP(0);
		gyro_still_det->acc_var[Y] = INT_TO_FP(0);
		gyro_still_det->acc_var[Z] = INT_TO_FP(0);
	} else {
		/*
		 * Check to see if we have enough samples to compute a stillness
		 * confidence score.
		 */
		gyro_still_det->stillness_window_ready =
			(sample_time >= stillness_win_endtime) &&
			(gyro_still_det->num_acc_samples > 1);
	}

	/* Record the most recent sample time stamp. */
	gyro_still_det->last_sample_time = sample_time;

	/* Online window mean and variance ("one-pass" accumulation). */
	gyro_still_det->num_acc_win_samples++;

	delta = (x - gyro_still_det->assumed_mean[X]);
	gyro_still_det->win_mean[X] += delta;
	gyro_still_det->acc_var[X] += fp_sq(delta);

	delta = (y - gyro_still_det->assumed_mean[Y]);
	gyro_still_det->win_mean[Y] += delta;
	gyro_still_det->acc_var[Y] += fp_sq(delta);

	delta = (z - gyro_still_det->assumed_mean[Z]);
	gyro_still_det->win_mean[Z] += delta;
	gyro_still_det->acc_var[Z] += fp_sq(delta);
}

fp_t gyro_still_det_compute(struct gyro_still_det *gyro_still_det)
{
	fp_t tmp_denom = INT_TO_FP(1);
	fp_t tmp_denom_mean = INT_TO_FP(1);
	fp_t tmp;
	fp_t upper_var_thresh, lower_var_thresh;

	/* Don't divide by zero (not likely, but a precaution). */
	if (gyro_still_det->num_acc_win_samples > 1) {
		tmp_denom = fp_div(
			tmp_denom,
			INT_TO_FP(gyro_still_det->num_acc_win_samples - 1));
		tmp_denom_mean =
			fp_div(tmp_denom_mean,
			       INT_TO_FP(gyro_still_det->num_acc_win_samples));
	} else {
		/* Return zero stillness confidence. */
		gyro_still_det->stillness_confidence = 0;
		return gyro_still_det->stillness_confidence;
	}

	/* Update the final calculation of window mean and variance. */
	tmp = gyro_still_det->win_mean[X];
	gyro_still_det->win_mean[X] =
		fp_mul(gyro_still_det->win_mean[X], tmp_denom_mean);
	gyro_still_det->win_var[X] =
		fp_mul((gyro_still_det->acc_var[X] -
			fp_mul(gyro_still_det->win_mean[X], tmp)),
		       tmp_denom);

	tmp = gyro_still_det->win_mean[Y];
	gyro_still_det->win_mean[Y] =
		fp_mul(gyro_still_det->win_mean[Y], tmp_denom_mean);
	gyro_still_det->win_var[Y] =
		fp_mul((gyro_still_det->acc_var[Y] -
			fp_mul(gyro_still_det->win_mean[Y], tmp)),
		       tmp_denom);

	tmp = gyro_still_det->win_mean[Z];
	gyro_still_det->win_mean[Z] =
		fp_mul(gyro_still_det->win_mean[Z], tmp_denom_mean);
	gyro_still_det->win_var[Z] =
		fp_mul((gyro_still_det->acc_var[Z] -
			fp_mul(gyro_still_det->win_mean[Z], tmp)),
		       tmp_denom);

	/* Adds the assumed mean value back to the total mean calculation. */
	gyro_still_det->win_mean[X] += gyro_still_det->assumed_mean[X];
	gyro_still_det->win_mean[Y] += gyro_still_det->assumed_mean[Y];
	gyro_still_det->win_mean[Z] += gyro_still_det->assumed_mean[Z];

	/* Define the variance thresholds. */
	upper_var_thresh = gyro_still_det->var_threshold +
			   gyro_still_det->confidence_delta;

	lower_var_thresh = gyro_still_det->var_threshold -
			   gyro_still_det->confidence_delta;

	/* Compute the stillness confidence score. */
	if ((gyro_still_det->win_var[X] > upper_var_thresh) ||
	    (gyro_still_det->win_var[Y] > upper_var_thresh) ||
	    (gyro_still_det->win_var[Z] > upper_var_thresh)) {
		/*
		 * Sensor variance exceeds the upper threshold (i.e., motion
		 * detected). Set stillness confidence equal to 0.
		 */
		gyro_still_det->stillness_confidence = 0;
	} else if ((gyro_still_det->win_var[X] <= lower_var_thresh) &&
		   (gyro_still_det->win_var[Y] <= lower_var_thresh) &&
		   (gyro_still_det->win_var[Z] <= lower_var_thresh)) {
		/*
		 * Sensor variance is below the lower threshold (i.e.
		 * stillness detected).
		 * Set stillness confidence equal to 1.
		 */
		gyro_still_det->stillness_confidence = INT_TO_FP(1);
	} else {
		/*
		 * Motion detection thresholds not exceeded. Compute the
		 * stillness confidence score.
		 */
		fp_t var_thresh = gyro_still_det->var_threshold;
		fpv3_t limit;

		/*
		 * Compute the stillness confidence score.
		 * Each axis score is limited [0,1].
		 */
		tmp_denom = fp_div(INT_TO_FP(1),
				   (upper_var_thresh - lower_var_thresh));
		limit[X] = gyro_still_det_limit(
			FLOAT_TO_FP(0.5f) -
			fp_mul(gyro_still_det->win_var[X] - var_thresh,
			       tmp_denom));
		limit[Y] = gyro_still_det_limit(
			FLOAT_TO_FP(0.5f) -
			fp_mul(gyro_still_det->win_var[Y] - var_thresh,
			       tmp_denom));
		limit[Z] = gyro_still_det_limit(
			FLOAT_TO_FP(0.5f) -
			fp_mul(gyro_still_det->win_var[Z] - var_thresh,
			       tmp_denom));

		gyro_still_det->stillness_confidence =
			fp_mul(limit[X], fp_mul(limit[Y], limit[Z]));
	}

	/* Return the stillness confidence. */
	return gyro_still_det->stillness_confidence;
}

void gyro_still_det_reset(struct gyro_still_det *gyro_still_det,
			  bool reset_stats)
{
	fp_t tmp_denom = INT_TO_FP(1);

	/* Reset the stillness data ready flag. */
	gyro_still_det->stillness_window_ready = false;

	/* Signal to start capture of next stillness data window. */
	gyro_still_det->start_new_window = true;

	/* Track the stillness confidence (current->previous). */
	gyro_still_det->prev_stillness_confidence =
		gyro_still_det->stillness_confidence;

	/* Track changes in the mean estimate. */
	if (gyro_still_det->num_acc_samples > INT_TO_FP(1))
		tmp_denom =
			fp_div(INT_TO_FP(1), gyro_still_det->num_acc_samples);

	gyro_still_det->prev_mean[X] =
		fp_mul(gyro_still_det->mean[X], tmp_denom);
	gyro_still_det->prev_mean[Y] =
		fp_mul(gyro_still_det->mean[Y], tmp_denom);
	gyro_still_det->prev_mean[Z] =
		fp_mul(gyro_still_det->mean[Z], tmp_denom);

	/* Reset the current statistics to zero. */
	if (reset_stats) {
		gyro_still_det->num_acc_samples = 0;
		gyro_still_det->mean[X] = INT_TO_FP(0);
		gyro_still_det->mean[Y] = INT_TO_FP(0);
		gyro_still_det->mean[Z] = INT_TO_FP(0);
		gyro_still_det->acc_var[X] = INT_TO_FP(0);
		gyro_still_det->acc_var[Y] = INT_TO_FP(0);
		gyro_still_det->acc_var[Z] = INT_TO_FP(0);
	}
}

fp_t gyro_still_det_limit(fp_t value)
{
	if (value < INT_TO_FP(0))
		value = INT_TO_FP(0);
	else if (value > INT_TO_FP(1))
		value = INT_TO_FP(1);

	return value;
}
