/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Header for motion_sense.c */

#ifndef __CROS_EC_MOTION_SENSE_H
#define __CROS_EC_MOTION_SENSE_H

#include "gpio.h"
#include "math_util.h"
#include "chipset.h"

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

#define SENSOR_ACTIVE_S5 CHIPSET_STATE_SOFT_OFF
#define SENSOR_ACTIVE_S3 CHIPSET_STATE_SUSPEND
#define SENSOR_ACTIVE_S0 CHIPSET_STATE_ON
#define SENSOR_ACTIVE_S0_S3 (SENSOR_ACTIVE_S3 | SENSOR_ACTIVE_S0)
#define SENSOR_ACTIVE_S0_S3_S5 (SENSOR_ACTIVE_S0_S3 | SENSOR_ACTIVE_S5)

struct motion_sensor_t {
	/* RO fields */
	uint32_t active_mask;
	char *name;
	enum sensor_chip_t chip;
	enum sensor_type_t type;
	enum sensor_location_t location;
	const struct accelgyro_drv *drv;
	struct mutex *mutex;
	void *drv_data;
	uint8_t i2c_addr;
	const matrix_3x3_t *rot_standard_ref;

	/* Default configuration parameters, RO only */
	int default_odr;
	int default_range;

	/* Run-Time configuration parameters */
	int odr;
	int range;

	/* state parameters */
	enum sensor_state state;
	enum chipset_state_mask active;
	vector_3_t xyz;
};

/* Defined at board level. */
extern struct motion_sensor_t motion_sensors[];
extern const unsigned int motion_sensor_count;

/*
 * Priority of the motion sense resume/suspend hooks, to be sure associated
 * hooks are scheduled properly.
 */
#define MOTION_SENSE_HOOK_PRIO (HOOK_PRIO_DEFAULT)

#ifdef CONFIG_ACCEL_INTERRUPTS
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
#endif

#endif /* __CROS_EC_MOTION_SENSE_H */
