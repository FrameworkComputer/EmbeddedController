/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_GYRO_CAL_H
#define __CROS_EC_GYRO_CAL_H

#include "common.h"
#include "gyro_still_det.h"
#include "math_util.h"
#include "stdbool.h"
#include "stddef.h"
#include "vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

struct temperature_mean_data {
	int16_t temperature_min_kelvin;
	int16_t temperature_max_kelvin;
	int16_t latest_temperature_kelvin;
	int mean_accumulator;
	size_t num_points;
};

/** Data structure for tracking min/max window mean during device stillness. */
struct min_max_window_mean_data {
	fpv3_t gyro_winmean_min;
	fpv3_t gyro_winmean_max;
};

struct gyro_cal {
	/** Stillness detector for accelerometer. */
	struct gyro_still_det accel_stillness_detect;
	/** Stillness detector for magnetometer. */
	struct gyro_still_det mag_stillness_detect;
	/** Stillness detector for gyroscope. */
	struct gyro_still_det gyro_stillness_detect;

	/**
	 * Data for tracking temperature mean during periods of device
	 * stillness.
	 */
	struct temperature_mean_data temperature_mean_tracker;

	/** Data for tracking gyro mean during periods of device stillness. */
	struct min_max_window_mean_data window_mean_tracker;

	/**
	 * Aggregated sensor stillness threshold required for gyro bias
	 * calibration.
	 */
	fp_t stillness_threshold;

	/** Min and max durations for gyro bias calibration. */
	uint32_t min_still_duration_us;
	uint32_t max_still_duration_us;

	/** Duration of the stillness processing windows. */
	uint32_t window_time_duration_us;

	/** Timestamp when device started a still period. */
	uint64_t start_still_time_us;

	/**
	 * Gyro offset estimate, and the associated calibration temperature,
	 * timestamp, and stillness confidence values.
	 * [rad/sec]
	 */
	fp_t bias_x, bias_y, bias_z;
	int bias_temperature_kelvin;
	fp_t stillness_confidence;
	uint32_t calibration_time_us;

	/**
	 * Current window end-time for all sensors. Used to assist in keeping
	 * sensor data collection in sync. On initialization this will be set to
	 * zero indicating that sensor data will be dropped until a valid
	 * end-time is set from the first gyro timestamp received.
	 */
	uint32_t stillness_win_endtime_us;

	/**
	 * Watchdog timer to reset to a known good state when data capture
	 * stalls.
	 */
	uint32_t gyro_window_start_us;
	uint32_t gyro_window_timeout_duration_us;

	/** Flag is "true" when the magnetometer is used. */
	bool using_mag_sensor;

	/** Flag set by user to control whether calibrations are used. */
	bool gyro_calibration_enable;

	/** Flag is 'true' when a new calibration update is ready. */
	bool new_gyro_cal_available;

	/** Flag to indicate if device was previously still. */
	bool prev_still;

	/**
	 * Min and maximum stillness window mean. This is used to check the
	 * stability of the mean values computed for the gyroscope (i.e.
	 * provides further validation for stillness).
	 */
	fpv3_t gyro_winmean_min;
	fpv3_t gyro_winmean_max;
	fp_t stillness_mean_delta_limit;

	/**
	 * The mean temperature over the stillness period. The limit is used to
	 * check for temperature stability and provide a gate for when
	 * temperature is rapidly changing.
	 */
	fp_t temperature_mean_kelvin;
	fp_t temperature_delta_limit_kelvin;
};

/**
 * Data structure used to configure the gyroscope calibration in individual
 * sensors.
 */
struct gyro_cal_data {
	/** The gyro_cal struct to use. */
	struct gyro_cal gyro_cal;
	/** The sensor ID of the accelerometer to use. */
	uint8_t accel_sensor_id;
	/**
	 * The sensor ID of the accelerometer to use (use a number greater than
	 * SENSOR_COUNT to skip).
	 */
	uint8_t mag_sensor_id;
};

/** Reset trackers. */
void init_gyro_cal(struct gyro_cal *gyro_cal);

/** Get the most recent bias calibration value. */
void gyro_cal_get_bias(struct gyro_cal *gyro_cal, fpv3_t bias,
		       int *temperature_kelvin, uint32_t *calibration_time_us);

/** Set an initial bias calibration value. */
void gyro_cal_set_bias(struct gyro_cal *gyro_cal, fpv3_t bias,
		       int temperature_kelvin, uint32_t calibration_time_us);

/** Remove gyro bias from the calibration [rad/sec]. */
void gyro_cal_remove_bias(struct gyro_cal *gyro_cal, fpv3_t in, fpv3_t out);

/** Returns true when a new gyro calibration is available. */
bool gyro_cal_new_bias_available(struct gyro_cal *gyro_cal);

/** Update the gyro calibration with gyro data [rad/sec]. */
void gyro_cal_update_gyro(struct gyro_cal *gyro_cal, uint32_t sample_time_us,
			  fp_t x, fp_t y, fp_t z, int temperature_kelvin);

/** Update the gyro calibration with mag data [micro Tesla]. */
void gyro_cal_update_mag(struct gyro_cal *gyro_cal, uint32_t sample_time_us,
			 fp_t x, fp_t y, fp_t z);

/** Update the gyro calibration with accel data [m/sec^2]. */
void gyro_cal_update_accel(struct gyro_cal *gyro_cal, uint32_t sample_time_us,
			   fp_t x, fp_t y, fp_t z);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_GYRO_CAL_H */
