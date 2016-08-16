/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Thermistor module for Chrome EC */

#ifndef __CROS_EC_TEMP_SENSOR_THERMISTOR_H
#define __CROS_EC_TEMP_SENSOR_THERMISTOR_H

struct thermistor_data_pair {
	uint8_t mv;	/* Scaled voltage level at ADC (in mV) */
	uint8_t temp;	/* Temperature in Celsius */
};

struct thermistor_info {
	uint8_t scaling_factor;	/* Scaling factor for voltage in data pair. */
	uint8_t num_pairs;	/* Number of data pairs. */

	/*
	 * Values between given data pairs will be calculated as points on
	 * a line. Pairs can be derived using the Steinhart-Hart equation.
	 *
	 * Guidelines for data sets:
	 * - Must contain at least two pairs.
	 * - First and last pairs are the max and min.
	 * - Pairs must be sorted in descending order.
	 * - 5 pairs should provide reasonable accuracy in most cases. Use
	 *   points where the slope changes significantly or to recalibrate
	 *   the algorithm if needed.
	 */
	const struct thermistor_data_pair *data;
};

/**
 * @brief Calculate temperature using linear interpolation of data points.
 *
 * Given a set of datapoints, the algorithm will calculate the "step" in
 * between each one in order to interpolate missing entries.
 *
 * @param mv	Value read from ADC (in millivolts).
 * @param info	Reference data set and info.
 *
 * @return	temperature in C
 */
int thermistor_linear_interpolate(uint16_t mv,
				const struct thermistor_info *info);

/**
 * ncp15wb temperature conversion routine.
 *
 * @param adc	10bit raw data on adc.
 *
 * @return	temperature in C.
 */
int ncp15wb_calculate_temp(uint16_t adc);

#endif  /* __CROS_EC_TEMP_SENSOR_THERMISTOR_NCP15WB_H */
