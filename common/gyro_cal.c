/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gyro_cal.h"
#include "string.h"
#include <stdbool.h>

/*
 * Maximum gyro bias correction (should be set based on expected max bias
 * of the given sensor). [rad/sec]
 */
#define MAX_GYRO_BIAS FLOAT_TO_FP(0.2f)

static void device_stillness_check(struct gyro_cal *gyro_cal,
				   uint32_t sample_time_us);

static void compute_gyro_cal(struct gyro_cal *gyro_cal,
			     uint32_t calibration_time_us);

static void check_window(struct gyro_cal *gyro_cal, uint32_t sample_time_us);

/** Data tracker command enumeration. */
enum gyro_cal_tracker_command {
	/** Resets the local data used for data tracking. */
	DO_RESET = 0,
	/** Updates the local tracking data. */
	DO_UPDATE_DATA,
	/** Stores intermediate results for later recall. */
	DO_STORE_DATA,
	/** Computes and provides the results of the gate function. */
	DO_EVALUATE
};

/**
 * Reset the gyro_cal's temperature statistics.
 *
 * @param gyro_cal Pointer to the gyro_cal data structure.
 */
static void gyro_temperature_stats_tracker_reset(struct gyro_cal *gyro_cal);

/**
 * Updates the temperature min/max and mean during the stillness period.
 *
 * @param gyro_cal		Pointer to the gyro_cal data structure.
 * @param temperature_kelvin	New temperature sample to include.
 */
static void gyro_temperature_stats_tracker_update(struct gyro_cal *gyro_cal,
						  int temperature_kelvin);

/**
 * Store the tracker data to be used for calculation.
 *
 * @param gyro_cal Pointer to the gyro_cal data structure.
 */
static void gyro_temperature_stats_tracker_store(struct gyro_cal *gyro_cal);

/**
 * Compute whether or not the temperature values are in range.
 *
 * @param gyro_cal	Pointer to the gyro_cal data structure.
 * @return		'true' if the min and max temperature values exceed the
 *			range set by 'temperature_delta_limit_kelvin'.
 */
static bool gyro_temperature_stats_tracker_eval(struct gyro_cal *gyro_cal);

/**
 * Tracks the minimum and maximum gyroscope stillness window means.
 * Returns
 *
 * @param gyro_cal Pointer to the gyro_cal data structure.
 * @param do_this  Command enumerator that controls function behavior.
 */
static void gyro_still_mean_tracker_reset(struct gyro_cal *gyro_cal);

/**
 * Compute the min/max window mean values according to 'window_mean_tracker'.
 *
 * @param gyro_cal Pointer to the gyro_cal data structure.
 */
static void gyro_still_mean_tracker_update(struct gyro_cal *gyro_cal);

/**
 * Store the most recent "stillness" mean data to the gyro_cal data structure.
 *
 * @param gyro_cal Pointer to the gyro_cal data structure.
 */
static void gyro_still_mean_tracker_store(struct gyro_cal *gyro_cal);

/**
 * Compute whether or not the gyroscope window range is within the valid range.
 *
 * @param gyro_cal	Pointer to the gyro_cal data structure.
 * @return		'true' when the difference between gyroscope min and max
 *			window means are outside the range set by
 *			'stillness_mean_delta_limit'.
 */
static bool gyro_still_mean_tracker_eval(struct gyro_cal *gyro_cal);

void init_gyro_cal(struct gyro_cal *gyro_cal)
{
	gyro_still_mean_tracker_reset(gyro_cal);
	gyro_temperature_stats_tracker_reset(gyro_cal);
}

void gyro_cal_get_bias(struct gyro_cal *gyro_cal, fpv3_t bias,
		       int *temperature_kelvin, uint32_t *calibration_time_us)
{
	bias[X] = gyro_cal->bias_x;
	bias[Y] = gyro_cal->bias_y;
	bias[Z] = gyro_cal->bias_z;
	*calibration_time_us = gyro_cal->calibration_time_us;
	*temperature_kelvin = gyro_cal->bias_temperature_kelvin;
}

void gyro_cal_set_bias(struct gyro_cal *gyro_cal, fpv3_t bias,
		       int temperature_kelvin, uint32_t calibration_time_us)
{
	gyro_cal->bias_x = bias[X];
	gyro_cal->bias_y = bias[Y];
	gyro_cal->bias_z = bias[Z];
	gyro_cal->calibration_time_us = calibration_time_us;
	gyro_cal->bias_temperature_kelvin = temperature_kelvin;
}

void gyro_cal_remove_bias(struct gyro_cal *gyro_cal, fpv3_t in, fpv3_t out)
{
	if (gyro_cal->gyro_calibration_enable) {
		out[X] = in[X] - gyro_cal->bias_x;
		out[Y] = in[Y] - gyro_cal->bias_y;
		out[Z] = in[Z] - gyro_cal->bias_z;
	}
}

bool gyro_cal_new_bias_available(struct gyro_cal *gyro_cal)
{
	bool new_gyro_cal_available = (gyro_cal->gyro_calibration_enable &&
				       gyro_cal->new_gyro_cal_available);

	/* Clear the flag. */
	gyro_cal->new_gyro_cal_available = false;

	return new_gyro_cal_available;
}

void gyro_cal_update_gyro(struct gyro_cal *gyro_cal, uint32_t sample_time_us,
			  fp_t x, fp_t y, fp_t z, int temperature_kelvin)
{
	/*
	 * Make sure that a valid window end-time is set, and start the window
	 * timer.
	 */
	if (gyro_cal->stillness_win_endtime_us <= 0) {
		gyro_cal->stillness_win_endtime_us =
			sample_time_us + gyro_cal->window_time_duration_us;

		/* Start the window timer. */
		gyro_cal->gyro_window_start_us = sample_time_us;
	}

	/* Update the temperature statistics. */
	gyro_temperature_stats_tracker_update(gyro_cal, temperature_kelvin);

	/* Pass gyro data to stillness detector */
	gyro_still_det_update(&gyro_cal->gyro_stillness_detect,
			      gyro_cal->stillness_win_endtime_us,
			      sample_time_us, x, y, z);

	/*
	 * Perform a device stillness check, set next window end-time, and
	 * possibly do a gyro bias calibration and stillness detector reset.
	 */
	device_stillness_check(gyro_cal, sample_time_us);
}

void gyro_cal_update_mag(struct gyro_cal *gyro_cal, uint32_t sample_time_us,
			 fp_t x, fp_t y, fp_t z)
{
	/* Pass magnetometer data to stillness detector. */
	gyro_still_det_update(&gyro_cal->mag_stillness_detect,
			      gyro_cal->stillness_win_endtime_us,
			      sample_time_us, x, y, z);

	/* Received a magnetometer sample; incorporate it into detection. */
	gyro_cal->using_mag_sensor = true;

	/*
	 * Perform a device stillness check, set next window end-time, and
	 * possibly do a gyro bias calibration and stillness detector reset.
	 */
	device_stillness_check(gyro_cal, sample_time_us);
}

void gyro_cal_update_accel(struct gyro_cal *gyro_cal, uint32_t sample_time_us,
			   fp_t x, fp_t y, fp_t z)
{
	/* Pass accelerometer data to stillnesss detector. */
	gyro_still_det_update(&gyro_cal->accel_stillness_detect,
			      gyro_cal->stillness_win_endtime_us,
			      sample_time_us, x, y, z);

	/*
	 * Perform a device stillness check, set next window end-time, and
	 * possibly do a gyro bias calibration and stillness detector reset.
	 */
	device_stillness_check(gyro_cal, sample_time_us);
}

/**
 * Handle the case where the device is found to be still. This function should
 * be called from device_stillness_check.
 *
 * @param gyro_cal Pointer to the gyroscope calibration struct.
 */
static void handle_device_is_still(struct gyro_cal *gyro_cal)
{
	/*
	 * Device is "still" logic:
	 * If not previously still, then record the start time.
	 * If stillness period is too long, then do a calibration.
	 * Otherwise, continue collecting stillness data.
	 */
	bool stillness_duration_exceeded = false;

	/*
	 * If device was not previously still, set new start timestamp.
	 */
	if (!gyro_cal->prev_still) {
		/*
		 * Record the starting timestamp of the current stillness
		 * window. This enables the calculation of total duration of
		 * the stillness period.
		 */
		gyro_cal->start_still_time_us =
			gyro_cal->gyro_stillness_detect.window_start_time;
	}

	/*
	 * Check to see if current stillness period exceeds the desired limit.
	 */
	stillness_duration_exceeded =
		gyro_cal->gyro_stillness_detect.last_sample_time >=
		(gyro_cal->start_still_time_us +
		 gyro_cal->max_still_duration_us);

	/* Track the new stillness mean and temperature data. */
	gyro_still_mean_tracker_store(gyro_cal);
	gyro_temperature_stats_tracker_store(gyro_cal);

	if (stillness_duration_exceeded) {
		/*
		 * The current stillness has gone too long. Do a calibration
		 * with the current data and reset.
		 */

		/*
		 * Updates the gyro bias estimate with the current window data
		 * and resets the stats.
		 */
		gyro_still_det_reset(&gyro_cal->accel_stillness_detect,
				     /*reset_stats=*/true);
		gyro_still_det_reset(&gyro_cal->gyro_stillness_detect,
				     /*reset_stats=*/true);
		gyro_still_det_reset(&gyro_cal->mag_stillness_detect,
				     /*reset_stats=*/true);

		/*
		 * Resets the local calculations because the stillness
		 * period is over.
		 */
		gyro_still_mean_tracker_reset(gyro_cal);
		gyro_temperature_stats_tracker_reset(gyro_cal);

		/* Computes a new gyro offset estimate. */
		compute_gyro_cal(
			gyro_cal,
			gyro_cal->gyro_stillness_detect.last_sample_time);

		/*
		 * Update stillness flag. Force the start of a new
		 * stillness period.
		 */
		gyro_cal->prev_still = false;
	} else {
		/* Continue collecting stillness data. */

		/* Extend the stillness period. */
		gyro_still_det_reset(&gyro_cal->accel_stillness_detect,
				     /*reset_stats=*/false);
		gyro_still_det_reset(&gyro_cal->gyro_stillness_detect,
				     /*reset_stats=*/false);
		gyro_still_det_reset(&gyro_cal->mag_stillness_detect,
				     /*reset_stats=*/false);

		/* Update the stillness flag. */
		gyro_cal->prev_still = true;
	}
}

static void handle_device_not_still(struct gyro_cal *gyro_cal)
{
	/* Device is NOT still; motion detected. */

	/*
	 * If device was previously still and the total stillness
	 * duration is not "too short", then do a calibration with the
	 * data accumulated thus far.
	 */
	bool stillness_duration_too_short =
		gyro_cal->gyro_stillness_detect.window_start_time <
		(gyro_cal->start_still_time_us +
		 gyro_cal->min_still_duration_us);

	if (gyro_cal->prev_still && !stillness_duration_too_short)
		compute_gyro_cal(
			gyro_cal,
			gyro_cal->gyro_stillness_detect.window_start_time);

	/* Reset the stillness detectors and the stats. */
	gyro_still_det_reset(&gyro_cal->accel_stillness_detect,
			     /*reset_stats=*/true);
	gyro_still_det_reset(&gyro_cal->gyro_stillness_detect,
			     /*reset_stats=*/true);
	gyro_still_det_reset(&gyro_cal->mag_stillness_detect,
			     /*reset_stats=*/true);

	/* Resets the temperature and sensor mean data. */
	gyro_temperature_stats_tracker_reset(gyro_cal);
	gyro_still_mean_tracker_reset(gyro_cal);

	/* Update stillness flag. */
	gyro_cal->prev_still = false;
}

void device_stillness_check(struct gyro_cal *gyro_cal, uint32_t sample_time_us)
{
	bool min_max_temp_exceeded = false;
	bool mean_not_stable = false;
	bool device_is_still = false;
	fp_t conf_not_rot = INT_TO_FP(0);
	fp_t conf_not_accel = INT_TO_FP(0);
	fp_t conf_still = INT_TO_FP(0);

	/* Check the window timer. */
	check_window(gyro_cal, sample_time_us);

	/* Is there enough data to do a stillness calculation? */
	if ((!gyro_cal->mag_stillness_detect.stillness_window_ready &&
	     gyro_cal->using_mag_sensor) ||
	    !gyro_cal->accel_stillness_detect.stillness_window_ready ||
	    !gyro_cal->gyro_stillness_detect.stillness_window_ready)
		return; /* Not yet, wait for more data. */

	/* Set the next window end-time for the stillness detectors. */
	gyro_cal->stillness_win_endtime_us =
		sample_time_us + gyro_cal->window_time_duration_us;

	/* Update the confidence scores for all sensors. */
	gyro_still_det_compute(&gyro_cal->accel_stillness_detect);
	gyro_still_det_compute(&gyro_cal->gyro_stillness_detect);
	if (gyro_cal->using_mag_sensor) {
		gyro_still_det_compute(&gyro_cal->mag_stillness_detect);
	} else {
		/*
		 * Not using magnetometer, force stillness confidence to 100%.
		 */
		gyro_cal->mag_stillness_detect.stillness_confidence =
			INT_TO_FP(1);
	}

	/* Updates the mean tracker data. */
	gyro_still_mean_tracker_update(gyro_cal);

	/*
	 * Determine motion confidence scores (rotation, accelerating, and
	 * stillness).
	 */
	conf_not_rot =
		fp_mul(gyro_cal->gyro_stillness_detect.stillness_confidence,
		       gyro_cal->mag_stillness_detect.stillness_confidence);
	conf_not_accel = gyro_cal->accel_stillness_detect.stillness_confidence;
	conf_still = fp_mul(conf_not_rot, conf_not_accel);

	/* Evaluate the mean and temperature gate functions. */
	mean_not_stable = gyro_still_mean_tracker_eval(gyro_cal);
	min_max_temp_exceeded = gyro_temperature_stats_tracker_eval(gyro_cal);

	/* Determines if the device is currently still. */
	device_is_still = (conf_still > gyro_cal->stillness_threshold) &&
			  !mean_not_stable && !min_max_temp_exceeded;

	if (device_is_still)
		handle_device_is_still(gyro_cal);
	else
		handle_device_not_still(gyro_cal);

	/* Reset the window timer after we have processed data. */
	gyro_cal->gyro_window_start_us = sample_time_us;
}

void compute_gyro_cal(struct gyro_cal *gyro_cal, uint32_t calibration_time_us)
{
	/* Check to see if new calibration values is within acceptable range. */
	if (!(gyro_cal->gyro_stillness_detect.prev_mean[X] < MAX_GYRO_BIAS &&
	      gyro_cal->gyro_stillness_detect.prev_mean[X] > -MAX_GYRO_BIAS &&
	      gyro_cal->gyro_stillness_detect.prev_mean[Y] < MAX_GYRO_BIAS &&
	      gyro_cal->gyro_stillness_detect.prev_mean[Y] > -MAX_GYRO_BIAS &&
	      gyro_cal->gyro_stillness_detect.prev_mean[Z] < MAX_GYRO_BIAS &&
	      gyro_cal->gyro_stillness_detect.prev_mean[Z] > -MAX_GYRO_BIAS))
		/* Outside of range. Ignore, reset, and continue. */
		return;

	/* Record the new gyro bias offset calibration. */
	gyro_cal->bias_x = gyro_cal->gyro_stillness_detect.prev_mean[X];
	gyro_cal->bias_y = gyro_cal->gyro_stillness_detect.prev_mean[Y];
	gyro_cal->bias_z = gyro_cal->gyro_stillness_detect.prev_mean[Z];

	/*
	 * Store the calibration temperature (using the mean temperature over
	 * the "stillness" period).
	 */
	gyro_cal->bias_temperature_kelvin = gyro_cal->temperature_mean_kelvin;

	/* Store the calibration time stamp. */
	gyro_cal->calibration_time_us = calibration_time_us;

	/* Record the final stillness confidence. */
	gyro_cal->stillness_confidence = fp_mul(
		gyro_cal->gyro_stillness_detect.prev_stillness_confidence,
		gyro_cal->accel_stillness_detect.prev_stillness_confidence);
	gyro_cal->stillness_confidence = fp_mul(
		gyro_cal->stillness_confidence,
		gyro_cal->mag_stillness_detect.prev_stillness_confidence);

	/* Set flag to indicate a new gyro calibration value is available. */
	gyro_cal->new_gyro_cal_available = true;
}

void check_window(struct gyro_cal *gyro_cal, uint32_t sample_time_us)
{
	bool window_timeout;

	/* Check for initialization of the window time (=0). */
	if (gyro_cal->gyro_window_start_us <= 0)
		return;

	/*
	 * Checks for the following window timeout conditions:
	 * i.  The current timestamp has exceeded the allowed window duration.
	 * ii. A timestamp was received that has jumped backwards by more than
	 *     the allowed window duration (e.g., timestamp clock roll-over).
	 */
	window_timeout =
		(sample_time_us > gyro_cal->gyro_window_timeout_duration_us +
					  gyro_cal->gyro_window_start_us) ||
		(sample_time_us + gyro_cal->gyro_window_timeout_duration_us <
		 gyro_cal->gyro_window_start_us);

	/* If a timeout occurred then reset to known good state. */
	if (window_timeout) {
		/* Reset stillness detectors and restart data capture. */
		gyro_still_det_reset(&gyro_cal->accel_stillness_detect,
				     /*reset_stats=*/true);
		gyro_still_det_reset(&gyro_cal->gyro_stillness_detect,
				     /*reset_stats=*/true);
		gyro_still_det_reset(&gyro_cal->mag_stillness_detect,
				     /*reset_stats=*/true);

		/* Resets the temperature and sensor mean data. */
		gyro_temperature_stats_tracker_reset(gyro_cal);
		gyro_still_mean_tracker_reset(gyro_cal);

		/* Resets the stillness window end-time. */
		gyro_cal->stillness_win_endtime_us = 0;

		/* Force stillness confidence to zero. */
		gyro_cal->accel_stillness_detect.prev_stillness_confidence = 0;
		gyro_cal->gyro_stillness_detect.prev_stillness_confidence = 0;
		gyro_cal->mag_stillness_detect.prev_stillness_confidence = 0;
		gyro_cal->stillness_confidence = 0;
		gyro_cal->prev_still = false;

		/*
		 * If there are no magnetometer samples being received then
		 * operate the calibration algorithm without this sensor.
		 */
		if (!gyro_cal->mag_stillness_detect.stillness_window_ready &&
		    gyro_cal->using_mag_sensor) {
			gyro_cal->using_mag_sensor = false;
		}

		/* Assert window timeout flags. */
		gyro_cal->gyro_window_start_us = 0;
	}
}

void gyro_temperature_stats_tracker_reset(struct gyro_cal *gyro_cal)
{
	/* Resets the mean accumulator. */
	gyro_cal->temperature_mean_tracker.num_points = 0;
	gyro_cal->temperature_mean_tracker.mean_accumulator = INT_TO_FP(0);

	/* Initializes the min/max temperatures values. */
	gyro_cal->temperature_mean_tracker.temperature_min_kelvin = 0x7fff;
	gyro_cal->temperature_mean_tracker.temperature_max_kelvin = 0xffff;
}

void gyro_temperature_stats_tracker_update(struct gyro_cal *gyro_cal,
					   int temperature_kelvin)
{
	/* Does the mean accumulation. */
	gyro_cal->temperature_mean_tracker.mean_accumulator +=
		temperature_kelvin;
	gyro_cal->temperature_mean_tracker.num_points++;

	/* Tracks the min, max, and latest temperature values. */
	gyro_cal->temperature_mean_tracker.latest_temperature_kelvin =
		temperature_kelvin;
	if (gyro_cal->temperature_mean_tracker.temperature_min_kelvin >
	    temperature_kelvin) {
		gyro_cal->temperature_mean_tracker.temperature_min_kelvin =
			temperature_kelvin;
	}
	if (gyro_cal->temperature_mean_tracker.temperature_max_kelvin <
	    temperature_kelvin) {
		gyro_cal->temperature_mean_tracker.temperature_max_kelvin =
			temperature_kelvin;
	}
}

void gyro_temperature_stats_tracker_store(struct gyro_cal *gyro_cal)
{
	/*
	 * Store the most recent temperature statistics data to the
	 * gyro_cal data structure. This functionality allows previous
	 * results to be recalled when the device suddenly becomes "not
	 * still".
	 */
	if (gyro_cal->temperature_mean_tracker.num_points > 0)
		gyro_cal->temperature_mean_kelvin =
			gyro_cal->temperature_mean_tracker.mean_accumulator /
			gyro_cal->temperature_mean_tracker.num_points;
	else
		gyro_cal->temperature_mean_kelvin =
			gyro_cal->temperature_mean_tracker
				.latest_temperature_kelvin;
}

bool gyro_temperature_stats_tracker_eval(struct gyro_cal *gyro_cal)
{
	bool min_max_temp_exceeded = false;

	/* Determines if the min/max delta exceeded the set limit. */
	if (gyro_cal->temperature_mean_tracker.num_points > 0) {
		min_max_temp_exceeded =
			(gyro_cal->temperature_mean_tracker
				 .temperature_max_kelvin -
			 gyro_cal->temperature_mean_tracker
				 .temperature_min_kelvin) >
			gyro_cal->temperature_delta_limit_kelvin;
	}

	return min_max_temp_exceeded;
}

void gyro_still_mean_tracker_reset(struct gyro_cal *gyro_cal)
{
	size_t i;

	/* Resets the min/max window mean values to a default value. */
	for (i = 0; i < 3; i++) {
		gyro_cal->window_mean_tracker.gyro_winmean_min[i] = FLT_MAX;
		gyro_cal->window_mean_tracker.gyro_winmean_max[i] = -FLT_MAX;
	}
}

void gyro_still_mean_tracker_update(struct gyro_cal *gyro_cal)
{
	int i;

	/* Computes the min/max window mean values. */
	for (i = 0; i < 3; ++i) {
		if (gyro_cal->window_mean_tracker.gyro_winmean_min[i] >
		    gyro_cal->gyro_stillness_detect.win_mean[i]) {
			gyro_cal->window_mean_tracker.gyro_winmean_min[i] =
				gyro_cal->gyro_stillness_detect.win_mean[i];
		}
		if (gyro_cal->window_mean_tracker.gyro_winmean_max[i] <
		    gyro_cal->gyro_stillness_detect.win_mean[i]) {
			gyro_cal->window_mean_tracker.gyro_winmean_max[i] =
				gyro_cal->gyro_stillness_detect.win_mean[i];
		}
	}
}

void gyro_still_mean_tracker_store(struct gyro_cal *gyro_cal)
{
	/*
	 * Store the most recent "stillness" mean data to the gyro_cal
	 * data structure. This functionality allows previous results to
	 * be recalled when the device suddenly becomes "not still".
	 */
	memcpy(gyro_cal->gyro_winmean_min,
	       gyro_cal->window_mean_tracker.gyro_winmean_min,
	       sizeof(gyro_cal->window_mean_tracker.gyro_winmean_min));
	memcpy(gyro_cal->gyro_winmean_max,
	       gyro_cal->window_mean_tracker.gyro_winmean_max,
	       sizeof(gyro_cal->window_mean_tracker.gyro_winmean_max));
}

bool gyro_still_mean_tracker_eval(struct gyro_cal *gyro_cal)
{
	bool mean_not_stable = false;
	size_t i;

	/*
	 * Performs the stability check and returns the 'true' if the
	 * difference between min/max window mean value is outside the
	 * stable range.
	 */
	for (i = 0; i < 3 && !mean_not_stable; i++) {
		mean_not_stable |=
			(gyro_cal->window_mean_tracker.gyro_winmean_max[i] -
			 gyro_cal->window_mean_tracker.gyro_winmean_min[i]) >
			gyro_cal->stillness_mean_delta_limit;
	}

	return mean_not_stable;
}
