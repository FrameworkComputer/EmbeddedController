/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ACCELGYRO_H
#define __CROS_EC_ACCELGYRO_H

#include "motion_sense.h"

/* Header file for accelerometer / gyro drivers. */

/*
 * EC reports sensor data on 16 bits. For accel/gyro/mag.. the MSB is the sign.
 * For instance, for gravity,
 * real_value[in g] = measured_value * range >> 15
 */
#define MOTION_SCALING_FACTOR (1 << 15)
#define MOTION_ONE_G (9.80665f)

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
	int (*read)(const struct motion_sensor_t *s, intv3_t v);

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
	int (*get_range)(const struct motion_sensor_t *s);

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
	int (*get_resolution)(const struct motion_sensor_t *s);

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
	int (*get_data_rate)(const struct motion_sensor_t *s);


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
	/**
	 * Setter and getter methods for the sensor scale.
	 * @s Pointer to sensor data.
	 * @scale: scale to apply to raw data.
	 * @temp: temperature when calibration was done.
	 * @return EC_SUCCESS if successful, non-zero if error.
	 */
	int (*set_scale)(const struct motion_sensor_t *s,
				const uint16_t    *scale,
				int16_t    temp);
	int (*get_scale)(const struct motion_sensor_t *s,
				uint16_t   *scale,
				int16_t    *temp);
	int (*perform_calib)(const struct motion_sensor_t *s);
#ifdef CONFIG_ACCEL_INTERRUPTS
	/**
	 * handler for interrupts triggered by the sensor: it runs in task and
	 * process the events that triggered an interrupt.
	 * @s Pointer to sensor data.
	 * @event Event to process. May add other events for the next processor.
	 *
	 * Return EC_SUCCESS when one event is handled, EC_ERROR_NOT_HANDLED
	 * when no events have been processed.
	 */
	int (*irq_handler)(struct motion_sensor_t *s, uint32_t *event);
#endif
#ifdef CONFIG_GESTURE_DETECTION
	/**
	 * handler for setting/getting activity information.
	 * Manage the high level activity detection of the chip.
	 * @s Pointer to sensor data.
	 * @activity activity to work on
	 * @enable 1 to enable, 0 to disable
	 * @data additional data if needed, activity dependent.
	 */
	int (*manage_activity)(const struct motion_sensor_t *s,
			       enum motionsensor_activity activity,
			       int enable,
			       const struct ec_motion_sense_activity *data);
	/**
	 * List activities managed by the sensors.
	 * @s Pointer to sensor data.
	 * @enable bit mask of activities currently enabled.
	 * @disabled bit mask of activities currently disabled.
	 */
	int (*list_activities)(const struct motion_sensor_t *s,
			       uint32_t *enabled,
			       uint32_t *disabled);

#endif
};

/* Used to save sensor information */
struct accelgyro_saved_data_t {
	int odr;
	int range;
	uint16_t scale[3];
};

/* Calibration data */
struct als_calibration_t {
	/*
	 * Scale, uscale, and offset are used to correct the raw 16 bit ALS
	 * data and then to convert it to 32 bit using the following equations:
	 * raw_value += offset;
	 * adjusted_value = raw_value * scale + raw_value * uscale / 10000;
	 */
	uint16_t scale;
	uint16_t uscale;
	int16_t offset;
};

/* RGB ALS Calibration Data */
struct rgb_calibration_t {
	/*
	 * Each channel has a scaling factor for normalization, representing
	 * a value between 0 and 2 (1 is translated as 1 << 15)
	 */
	uint16_t scale;

	/* Any offset to add to raw channel data */
	int16_t offset;
};

/* als driver data */
struct als_drv_data_t {
	int rate;          /* holds current sensor rate */
	int last_value;    /* holds last als clear channel value */
	struct als_calibration_t als_cal;    /* calibration data */
};

#define SENSOR_APPLY_SCALE(_input, _scale) \
	(((_input) * (_scale)) / MOTION_SENSE_DEFAULT_SCALE)

/* Individual channel scale value between 0 and 2 represented in 16 bits */
#define ALS_CHANNEL_SCALE(_x) ((_x) << 15)

#endif /* __CROS_EC_ACCELGYRO_H */
