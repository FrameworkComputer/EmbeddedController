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

enum sensor_config {
	SENSOR_CONFIG_AP, /* Configuration requested/for the AP */
	SENSOR_CONFIG_EC_S0, /* Configuration from the EC while device in S0 */
	SENSOR_CONFIG_EC_S3, /* from the EC when device sleep */
	SENSOR_CONFIG_EC_S5, /* from the EC when device powered off */
	SENSOR_CONFIG_MAX,
};

#define SENSOR_ACTIVE_S5 CHIPSET_STATE_SOFT_OFF
#define SENSOR_ACTIVE_S3 CHIPSET_STATE_SUSPEND
#define SENSOR_ACTIVE_S0 CHIPSET_STATE_ON
#define SENSOR_ACTIVE_S0_S3 (SENSOR_ACTIVE_S3 | SENSOR_ACTIVE_S0)
#define SENSOR_ACTIVE_S0_S3_S5 (SENSOR_ACTIVE_S0_S3 | SENSOR_ACTIVE_S5)

/* Events the motion sense task may have to process.*/
#define TASK_EVENT_MOTION_FLUSH_PENDING     TASK_EVENT_CUSTOM(1)
#define TASK_EVENT_MOTION_ODR_CHANGE        TASK_EVENT_CUSTOM(2)
/* Next 8 events for sensor interrupt lines */
#define TASK_EVENT_MOTION_INTERRUPT_MASK    (0xff << 2)

/* Minimum time in between running motion sense task loop. */
#define MIN_MOTION_SENSE_WAIT_TIME (3 * MSEC)
#define MAX_MOTION_SENSE_WAIT_TIME (60000 * MSEC)

#define ROUND_UP_FLAG (1 << 31)
#define BASE_ODR(_odr) ((_odr) & ~ROUND_UP_FLAG)

struct motion_data_t {
	/*
	 * data rate the sensor will measure, in mHz: 0 suspended.
	 * MSB is used to know if we are rounding up.
	 * */
	unsigned odr;
	/* delay between collection by EC, in ms.
	 * For non FIFO sensor, should be need 1e6/odr to
	 * collect events.
	 * For sensor with FIFO, can be much longer.
	 * 0: no collection.
	 */
	unsigned short ec_rate;
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
	/* i2c address or SPI slave logic GPIO. */
	uint8_t addr;
	const matrix_3x3_t *rot_standard_ref;

	/*
	 * default_range: set by default by the EC.
	 * The host can change it, but rarely does.
	 */
	int default_range;

	/*
	 * There are 4 configuration parameters to deal with different
	 * configuration
	 *
	 * Power   |         S0        |            S3     |      S5
	 * --------+-------------------+-------------------+-----------------
	 * From AP | <------- SENSOR_CONFIG_AP ----------> |
	 *         | Use for normal    | While sleeping    | Always disabled
	 *         | operation: game,  | For Activity      |
	 *         | screen rotation   | Recognition       |
	 * --------+-------------------+-------------------+------------------
	 * From EC |SENSOR_CONFIG_EC_S0|SENSOR_CONFIG_EC_S3|SENSOR_CONFIG_EC_S5
	 *         | Background        | Gesture  Recognition (Double tap, ...)
	 *         | Activity: compass,|
	 *         | ambient light)|
	 */
	struct motion_data_t config[SENSOR_CONFIG_MAX];

	/* state parameters */
	enum sensor_state state;
	vector_3_t raw_xyz;
	vector_3_t xyz;

	/* How many flush events are pending */
	uint32_t flush_pending;

	/*
	 * Allow EC to request an higher frequency for the sensors than the AP.
	 */
	fp_t oversampling;

	/*
	 * How many vector events are lost in the FIFO since last time
	 * FIFO info has been transmitted.
	 */
	uint16_t lost;

	/*
	 * Time since last collection:
	 * For sensor with hardware FIFO,  time since last sample
	 * has move from the hardware FIFO to the FIFO (used if fifo rate != 0).
	 * For sensor without FIFO, time since the last event was collect
	 * from sensor registers.
	 */
	 uint32_t last_collection;
};

/* Defined at board level. */
extern struct motion_sensor_t motion_sensors[];
extern const unsigned motion_sensor_count;

/* For testing purposes: export the sampling interval. */
extern enum chipset_state_mask sensor_active;
extern unsigned motion_interval;
int motion_sense_set_motion_intervals(void);

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

/**
 * Interrupt function for lid accelerometer.
 *
 * @param data data to insert in the FIFO
 * @param sensor sensor the data comes from
 * @valid_data data should be copied into the public sensor vector
 */
void motion_sense_fifo_add_unit(struct ec_response_motion_sensor_data *data,
				struct motion_sensor_t *sensor,
				int valid_data);

#endif

#ifdef CONFIG_GESTURE_HOST_DETECTION
/* Add an extra sensor. We may need to add more */
#define MOTION_SENSE_ACTIVITY_SENSOR_ID (motion_sensor_count)
#define ALL_MOTION_SENSORS (MOTION_SENSE_ACTIVITY_SENSOR_ID + 1)
#else
#define ALL_MOTION_SENSORS motion_sensor_count
#endif

#ifdef CONFIG_ALS_LIGHTBAR_DIMMING
#ifdef TEST_BUILD
#define MOTION_SENSE_LUX 0
#else
#define MOTION_SENSE_LUX motion_sensors[CONFIG_ALS_LIGHTBAR_DIMMING].raw_xyz[0]
#endif
#endif

#endif /* __CROS_EC_MOTION_SENSE_H */
