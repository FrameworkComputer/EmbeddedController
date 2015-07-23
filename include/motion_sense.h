/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Header for motion_sense.c */

#ifndef __CROS_EC_MOTION_SENSE_H
#define __CROS_EC_MOTION_SENSE_H

#include "chipset.h"
#include "common.h"
#include "ec_commands.h"
#include "gpio.h"
#include "math_util.h"
#include "queue.h"
#include "timer.h"

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

/* Events the motion sense task may have to process.*/
#define TASK_EVENT_MOTION_FLUSH_PENDING TASK_EVENT_CUSTOM(1)
#define TASK_EVENT_MOTION_INTERRUPT     TASK_EVENT_CUSTOM(2)
#define TASK_EVENT_MOTION_ODR_CHANGE    TASK_EVENT_CUSTOM(4)

/* Define sensor sampling interval in suspend. */
#ifdef CONFIG_GESTURE_DETECTION
#define SUSPEND_SAMPLING_INTERVAL (CONFIG_GESTURE_SAMPLING_INTERVAL_MS * MSEC)
#elif defined(CONFIG_ACCEL_FIFO)
#define SUSPEND_SAMPLING_INTERVAL (1000 * MSEC)
#else
#define SUSPEND_SAMPLING_INTERVAL (100 * MSEC)
#endif

/* Minimum time in between running motion sense task loop. */
#define MIN_MOTION_SENSE_WAIT_TIME (3 * MSEC)
#define MAX_MOTION_SENSE_WAIT_TIME (60000 * MSEC)

struct motion_data_t {
	/* data rate the sensor will measure, in mHz */
	int odr;
	/* range of measurement in SI */
	int range;
	/* delay between collection by EC, in ms.
	 * For non FIFO sensor, should be nead 1e6/odr to
	 * collect events.
	 * For sensor with FIFO, can be much longer.
	 */
	unsigned ec_rate;
};

struct motion_sensor_t {
	/* RO fields */
	uint32_t active_mask;
	char *name;
	enum motionsensor_chip chip;
	enum motionsensor_type type;
	enum motionsensor_location location;
	const struct accelgyro_drv *drv;
	struct mutex *mutex;
	void *drv_data;
	uint8_t i2c_addr;
	const matrix_3x3_t *rot_standard_ref;

	/* Default configuration parameters, RO only */
	struct motion_data_t default_config;

	/* Run-Time configuration parameters */
	struct motion_data_t runtime_config;

	/* state parameters */
	enum sensor_state state;
	vector_3_t raw_xyz;
	vector_3_t xyz;

	/* How many flush events are pending */
	uint32_t flush_pending;

	/*
	 * How many vector events are lost in the FIFO since last time
	 * FIFO info has been transmitted.
	 */
	uint16_t lost;

	/*
	 * Time since iast collection:
	 * For sensor with hardware FIFO,  time since last sample
	 * has move from the hardware FIFO to the FIFO (used if fifo rate != 0).
	 * For sensor without FIFO, time since the last event was collect
	 * from sensor registers.
	 */
	int last_collection;
};

/* Defined at board level. */
extern struct motion_sensor_t motion_sensors[];
extern const unsigned motion_sensor_count;

/* For testing purposes: export the sampling interval. */
extern unsigned accel_interval;
int motion_sense_set_accel_interval(
		struct motion_sensor_t *driving_sensor,
		unsigned data);

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

#ifdef CONFIG_ACCEL_FIFO
extern struct queue motion_sense_fifo;

void motion_sense_fifo_add_unit(struct ec_response_motion_sensor_data *data,
				const struct motion_sensor_t *sensor);

#endif
#endif /* __CROS_EC_MOTION_SENSE_H */
