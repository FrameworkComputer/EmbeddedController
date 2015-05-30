/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ACCELGYRO_H
#define __CROS_EC_ACCELGYRO_H

#include "motion_sense.h"

/* Header file for accelerometer / gyro drivers. */

/* Number of counts from accelerometer that represents 1G acceleration. */
#define ACCEL_G  1024

struct accelgyro_drv {
	/**
	 * Initialize accelerometers.
	 * @s Pointer to sensor data pointer. Sensor data will be
	 * allocated on success.
	 * @return EC_SUCCESS if successful, non-zero if error.
	 */
	int (*init)(const struct motion_sensor_t *s);

	/**
	 * Read all three accelerations of an accelerometer. Note that all
	 * three accelerations come back in counts, where ACCEL_G can be used
	 * to convert counts to engineering units.
	 * @s Pointer to sensor data.
	 * @v Vector to store acceleration (in units of counts).
	 * @return EC_SUCCESS if successful, non-zero if error.
	 */
	int (*read)(const struct motion_sensor_t *s, vector_3_t v);

	/**
	 * Setter and getter methods for the sensor range. The sensor range
	 * defines the maximum value that can be returned from read(). As the
	 * range increases, the resolution gets worse.
	 * @s Pointer to sensor data.
	 * @range Range (Units are +/- G's for accel, +/- deg/s for gyro)
	 * @rnd Rounding flag. If true, it rounds up to nearest valid
	 * value. Otherwise, it rounds down.
	 * @return EC_SUCCESS if successful, non-zero if error.
	 */
	int (*set_range)(const struct motion_sensor_t *s,
			int range,
			int rnd);
	int (*get_range)(const struct motion_sensor_t *s,
			int *range);

	/**
	 * Setter and getter methods for the sensor resolution.
	 * @s Pointer to sensor data.
	 * @range Resolution (Units are number of bits)
	 * param rnd Rounding flag. If true, it rounds up to nearest valid
	 * value. Otherwise, it rounds down.
	 * @return EC_SUCCESS if successful, non-zero if error.
	 */
	int (*set_resolution)(const struct motion_sensor_t *s,
				int res,
				int rnd);
	int (*get_resolution)(const struct motion_sensor_t *s,
				int *res);

	/**
	 * Setter and getter methods for the sensor output data range. As the
	 * ODR increases, the LPF roll-off frequency also increases.
	 * @s Pointer to sensor data.
	 * @rate Output data rate (units are milli-Hz)
	 * @rnd Rounding flag. If true, it rounds up to nearest valid
	 * value. Otherwise, it rounds down.
	 * @return EC_SUCCESS if successful, non-zero if error.
	 */
	int (*set_data_rate)(const struct motion_sensor_t *s,
				int rate,
				int rnd);
	int (*get_data_rate)(const struct motion_sensor_t *s,
				int *rate);


	/**
	 * Setter and getter methods for the sensor offset.
	 * @s Pointer to sensor data.
	 * @offset: offset to apply to raw data.
	 * @temp: temperature when calibration was done.
	 * @return EC_SUCCESS if successful, non-zero if error.
	 */
	int (*set_offset)(const struct motion_sensor_t *s,
				const int16_t    *offset,
				int16_t    temp);
	int (*get_offset)(const struct motion_sensor_t *s,
				int16_t    *offset,
				int16_t    *temp);
#ifdef CONFIG_ACCEL_INTERRUPTS
	/**
	 * Setup a one-time accel interrupt. If the threshold is low enough, the
	 * interrupt may trigger due simply to noise and not any real motion.
	 * If the threshold is 0, the interrupt will fire immediately.
	 * @s Pointer to sensor data.
	 * @threshold Threshold for interrupt in units of counts.
	 */
	int (*set_interrupt)(const struct motion_sensor_t *s,
			     unsigned int threshold);

	/**
	 * handler for interrupts triggered by the sensor: it runs in task and
	 * process the events that triggered an interrupt.
	 * @s Pointer to sensor data.
	 */
	int (*irq_handler)(const struct motion_sensor_t *s);
#endif
#ifdef CONFIG_ACCEL_FIFO
	/**
	 * Retrieve hardware FIFO from sensor,
	 * - put data in Sensor Hub fifo.
	 * - update sensor raw_xyz vector with the last information.
	 * We put raw data in hub fifo and process data from theres.
	 * @s Pointer to sensor data.
	 */
	int (*load_fifo)(struct motion_sensor_t *s);
#endif
};

#endif /* __CROS_EC_ACCELGYRO_H */
