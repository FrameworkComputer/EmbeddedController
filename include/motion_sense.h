/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Header for motion_sense.c */

#ifndef __CROS_EC_MOTION_SENSE_H
#define __CROS_EC_MOTION_SENSE_H

#include "gpio.h"
#include "math_util.h"

/* Anything outside of lid angle range [-180, 180] should work. */
#define LID_ANGLE_UNRELIABLE 500.0F

/**
 * This structure defines all of the data needed to specify the orientation
 * of the base and lid accelerometers in order to calculate the lid angle.
 */
struct accel_orientation {
	/*
	 * Rotation matrix to rotate the lid sensor into the same reference
	 * frame as the base sensor.
	 */
	matrix_3x3_t rot_align;

	/* Rotation matrix to rotate positive 90 degrees around the hinge. */
	matrix_3x3_t rot_hinge_90;

	/*
	 * Rotation matrix to rotate 180 degrees around the hinge. The value
	 * here should be rot_hinge_90 ^ 2.
	 */
	matrix_3x3_t rot_hinge_180;

	/*
	 * Rotation matrix to rotate base sensor into the standard reference
	 * frame.
	 */
	matrix_3x3_t rot_standard_ref;

	/* Vector pointing along hinge axis. */
	vector_3_t hinge_axis;
};

/* Link global structure for orientation. This must be defined in board.c. */
extern const struct accel_orientation acc_orient;


/**
 * Get last calculated lid angle. Note, the lid angle calculated by the EC
 * is un-calibrated and is an approximate angle.
 *
 * @return lid angle in degrees in range [0, 360].
 */
int motion_get_lid_angle(void);


/**
 * Interrupt function for lid accelerometer.
 *
 * @param signal GPIO signal that caused interrupt
 */
void accel_int_lid(enum gpio_signal signal);

/**
 * Interrupt function for base accelerometer.
 *
 * @param signal GPIO signal that caused interrupt
 */
void accel_int_base(enum gpio_signal signal);

enum sensor_location_t {
	LOCATION_BASE = 0,
	LOCATION_LID  = 1,
};

enum sensor_type_t {
	SENSOR_ACCELEROMETER = 0x1,
	SENSOR_GYRO          = 0x2,
};

enum sensor_chip_t {
	SENSOR_CHIP_KXCJ9 = 0,
	SENSOR_CHIP_LSM6DS0 = 1,
};

enum sensor_state {
	SENSOR_NOT_INITIALIZED = 0,
	SENSOR_INITIALIZED = 1,
	SENSOR_INIT_ERROR = 2
};

enum sensor_power {
	SENSOR_POWER_OFF = 0,
	SENSOR_POWER_ON  = 1
};

struct motion_sensor_t {
	/* RO fields */
	char *name;
	enum sensor_chip_t chip;
	enum sensor_type_t type;
	enum sensor_location_t location;
	const struct accelgyro_drv *drv;
	struct mutex *mutex;
	void *drv_data;
	uint8_t i2c_addr;

	/* RW fields */
	enum sensor_state state;
	enum sensor_power power;
	vector_3_t raw_xyz;
	vector_3_t xyz;
};

/* Defined at board level. */
extern struct motion_sensor_t motion_sensors[];
extern const unsigned int motion_sensor_count;

#endif /* __CROS_EC_MOTION_SENSE_H */
