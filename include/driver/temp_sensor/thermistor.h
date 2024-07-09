/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Thermistor module for Chrome EC */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#ifndef __CROS_EC_TEMP_SENSOR_THERMISTOR_H
#define __CROS_EC_TEMP_SENSOR_THERMISTOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct thermistor_data_pair {
	uint8_t mv; /* Scaled voltage level at ADC (in mV) */
	uint8_t temp; /* Temperature in Celsius */
};

struct thermistor_info {
	uint8_t scaling_factor; /* Scaling factor for voltage in data pair. */
	uint8_t num_pairs; /* Number of data pairs. */

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
 * Calculate temperature using linear interpolation of data points.
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

#ifdef CONFIG_THERMISTOR_NCP15WB
/**
 * ncp15wb temperature conversion routine.
 *
 * @param adc	10bit raw data on adc.
 *
 * @return	temperature in C.
 */
int ncp15wb_calculate_temp(uint16_t adc);
#endif /* CONFIG_THERMISTOR_NCP15WB */

#ifdef CONFIG_STEINHART_HART_3V3_13K7_47K_4050B
/**
 * Reads the specified ADC channel and uses a lookup table and interpolation to
 * return a temperature in degrees K.
 *
 * The lookup table is based off of a resistor divider circuit on 3.3V with a
 * 13.7K resistor in series with a thermistor with nominal value of 47K (at 25C)
 * and a B (25/100) value of 4050.
 *
 * @param idx_adc	The idx value from the temp_sensor_t struct, which is
 *			the ADC channel to read and convert to degrees K
 * @param temp_ptr	Destination for temperature (in degrees K)
 *
 * @return EC_SUCCESS, or non-zero if error.
 */
int get_temp_3v3_13k7_47k_4050b(int idx_adc, int *temp_ptr);
#endif

#ifdef CONFIG_STEINHART_HART_3V3_51K1_47K_4050B
/**
 * Reads the specified ADC channel and uses a lookup table and interpolation to
 * return a temperature in degrees K.
 *
 * The lookup table is based off of a resistor divider circuit on 3.3V with a
 * 51.1K resistor in series with a thermistor with nominal value of 47K (at 25C)
 * and a B (25/100) value of 4050.
 *
 * @param idx_adc	The idx value from the temp_sensor_t struct, which is
 *			the ADC channel to read and convert to degrees K
 * @param temp_ptr	Destination for temperature (in degrees K)
 *
 * @return EC_SUCCESS, or non-zero if error.
 */
int get_temp_3v3_51k1_47k_4050b(int idx_adc, int *temp_ptr);
#endif

#ifdef CONFIG_STEINHART_HART_6V0_51K1_47K_4050B
/**
 * Reads the specified ADC channel and uses a lookup table and interpolation to
 * return a temperature in degrees K.
 *
 * The lookup table is based off of a resistor divider circuit on 6.0V with a
 * 51.1K resistor in series with a thermistor with nominal value of 47K (at 25C)
 * and a B (25/100) value of 4050.
 *
 * @param idx_adc	The idx value from the temp_sensor_t struct, which is
 *			the ADC channel to read and convert to degrees K
 * @param temp_ptr	Destination for temperature (in degrees K)
 *
 * @return EC_SUCCESS, or non-zero if error.
 */
int get_temp_6v0_51k1_47k_4050b(int idx_adc, int *temp_ptr);
#endif

#ifdef CONFIG_STEINHART_HART_3V0_22K6_47K_4050B
/**
 * Reads the specified ADC channel and uses a lookup table and interpolation to
 * return a temperature in degrees K.
 *
 * The lookup table is based off of a resistor divider circuit on 3V with a
 * 22.6K resistor in series with a thermistor with nominal value of 47K (at 25C)
 * and a B (25/100) value of 4050.
 *
 * @param idx_adc	The idx value from the temp_sensor_t struct, which is
 *			the ADC channel to read and convert to degrees K
 * @param temp_ptr	Destination for temperature (in degrees K)
 *
 * @return EC_SUCCESS, or non-zero if error.
 */
int get_temp_3v0_22k6_47k_4050b(int idx_adc, int *temp_ptr);
#endif

#ifdef CONFIG_STEINHART_HART_3V3_30K9_47K_4050B
/**
 * Reads the specified ADC channel and uses a lookup table and interpolation to
 * return a temperature in degrees K.
 *
 * The lookup table is based off of a resistor divider circuit on 3.3V with a
 * 30.9K resistor in series with a thermistor with nominal value of 47K (at 25C)
 * and a B (25/100) value of 4050.
 *
 * @param idx_adc	The idx value from the temp_sensor_t struct, which is
 *			the ADC channel to read and convert to degrees K
 * @param temp_ptr	Destination for temperature (in degrees K)
 *
 * @return EC_SUCCESS, or non-zero if error.
 */
int get_temp_3v3_30k9_47k_4050b(int idx_adc, int *temp_ptr);
#endif

/**
 * Reads the sensor's ADC channel and uses a lookup table and interpolation to
 * argument thermistor_info for interpolation to return a temperature in degrees
 * K.
 *
 * @param idx_adc	The idx value from the temp_sensor_t struct, which is
 *			the ADC channel to read and convert to degrees K
 * @param temp_ptr	Destination for temperature (in degrees K)
 * @param info	Structure containing information about the underlying thermistor
 * that is necessary to interpolate temperature
 *
 * @return EC_SUCCESS, or non-zero if error.
 */
int thermistor_get_temperature(int idx_adc, int *temp_ptr,
			       const struct thermistor_info *info);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_TEMP_SENSOR_THERMISTOR_NCP15WB_H */
