/* Copyright 2014 The Chromium OS Authors. All rights reserved.
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
#include "i2c.h"
#include "math_util.h"
#include "queue.h"
#include "timer.h"
#include "util.h"

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

#define SENSOR_ACTIVE_S5 (CHIPSET_STATE_SOFT_OFF | CHIPSET_STATE_HARD_OFF)
#define SENSOR_ACTIVE_S3 CHIPSET_STATE_ANY_SUSPEND
#define SENSOR_ACTIVE_S0 CHIPSET_STATE_ON
#define SENSOR_ACTIVE_S0_S3 (SENSOR_ACTIVE_S3 | SENSOR_ACTIVE_S0)
#define SENSOR_ACTIVE_S0_S3_S5 (SENSOR_ACTIVE_S0_S3 | SENSOR_ACTIVE_S5)


/*
 * Events layout:
 * 0                       8              10
 * +-----------------------+---------------+----------------------------
 * | hardware interrupts   | internal ints | activity interrupts
 * +-----------------------+---------------+----------------------------
 */

/* First 8 events for sensor interrupt lines */
#define TASK_EVENT_MOTION_INTERRUPT_NUM      8
#define TASK_EVENT_MOTION_INTERRUPT_MASK \
	((1 << TASK_EVENT_MOTION_INTERRUPT_NUM) - 1)
#define TASK_EVENT_MOTION_SENSOR_INTERRUPT(_sensor_id) \
	BUILD_CHECK_INLINE( \
		TASK_EVENT_CUSTOM_BIT(_sensor_id), \
		_sensor_id < TASK_EVENT_MOTION_INTERRUPT_NUM)

/* Internal events to motion sense task.*/
#define TASK_EVENT_MOTION_FIRST_INTERNAL_EVENT TASK_EVENT_MOTION_INTERRUPT_NUM
#define TASK_EVENT_MOTION_INTERNAL_EVENT_NUM    2
#define TASK_EVENT_MOTION_FLUSH_PENDING \
	TASK_EVENT_CUSTOM_BIT(TASK_EVENT_MOTION_FIRST_INTERNAL_EVENT)
#define TASK_EVENT_MOTION_ODR_CHANGE \
	TASK_EVENT_CUSTOM_BIT(TASK_EVENT_MOTION_FIRST_INTERNAL_EVENT + 1)

/* Activity events */
#define TASK_EVENT_MOTION_FIRST_SW_EVENT   \
	(TASK_EVENT_MOTION_INTERRUPT_NUM + TASK_EVENT_MOTION_INTERNAL_EVENT_NUM)
#define TASK_EVENT_MOTION_ACTIVITY_INTERRUPT(_activity_id) \
	(TASK_EVENT_CUSTOM_BIT( \
		TASK_EVENT_MOTION_FIRST_SW_EVENT + (_activity_id)))


#define ROUND_UP_FLAG BIT(31)
#define BASE_ODR(_odr) ((_odr) & ~ROUND_UP_FLAG)
#define BASE_RANGE(_range) ((_range) & ~ROUND_UP_FLAG)

#ifdef CONFIG_ACCEL_FIFO
#define MAX_FIFO_EVENT_COUNT CONFIG_ACCEL_FIFO_SIZE
#else
#define MAX_FIFO_EVENT_COUNT 0
#endif

/*
 * I2C/SPI Slave Address encoding for motion sensors
 * - The generic defines, I2C_ADDR_MASK and I2C_IS_BIG_ENDIAN_MASK
 *   are defined in i2c.h.
 * - Motion sensors support some sensors on the SPI bus, so this
 *   overloads the I2C Address to use a single bit to indicate
 *   it is a SPI address instead of an I2C.  Since SPI does not
 *   use slave addressing, it is up to the driver to use this
 *   field as it sees fit
 */
#define SLAVE_MK_I2C_ADDR_FLAGS(addr)	(addr)
#define SLAVE_MK_SPI_ADDR_FLAGS(addr)	((addr) | I2C_FLAG_ADDR_IS_SPI)

#define SLAVE_GET_I2C_ADDR(addr_flags)	(I2C_GET_ADDR(addr_flags))
#define SLAVE_GET_SPI_ADDR(addr_flags)	((addr_flags) & I2C_ADDR_MASK)

#define SLAVE_IS_SPI(addr_flags)	((addr_flags) & I2C_FLAG_ADDR_IS_SPI)

/*
 * Define the frequency to use in max_frequency based on the maximal frequency
 * the sensor support and what the EC can provide.
 * Return a frequency the sensor supports.
 * Trigger a compilation error when the EC way to slow for the sensor.
 */
#define MOTION_MAX_SENSOR_FREQUENCY(_max, _step) GENERIC_MIN( \
	(_max) / (CONFIG_EC_MAX_SENSOR_FREQ_MILLIHZ >= (_step)), \
	(_step) << __fls(CONFIG_EC_MAX_SENSOR_FREQ_MILLIHZ / (_step)))

struct motion_data_t {
	/*
	 * data rate the sensor will measure, in mHz: 0 suspended.
	 * MSB is used to know if we are rounding up.
	 */
	unsigned int odr;

	/*
	 * delay between collection by EC, in us.
	 * For non FIFO sensor, should be near 1e9/odr to
	 * collect events.
	 * For sensor with FIFO, can be much longer.
	 * 0: no collection.
	 */
	unsigned int ec_rate;
};

/*
 * When set, spoof mode will allow the EC to report arbitrary values for any of
 * the components.
 */
#define MOTIONSENSE_FLAG_IN_SPOOF_MODE		BIT(1)
#define MOTIONSENSE_FLAG_INT_SIGNAL		BIT(2)
#define MOTIONSENSE_FLAG_INT_ACTIVE_HIGH	BIT(3)

struct online_calib_data {
	/**
	 * Type specific data.
	 * - For Accelerometers use struct accel_cal.
	 * - For Gyroscopes (not yet implemented).
	 * - For Magnetormeters (not yet implemented).
	 */
	void *type_specific_data;

	/**
	 * Cached calibration values from the latest successful calibration
	 * pass.
	 */
	int16_t cache[3];

	/** The latest temperature reading in K, negative if not set. */
	int last_temperature;

	/** Timestamp for the latest temperature reading. */
	uint32_t last_temperature_timestamp;
};

struct motion_sensor_t {
	/* RO fields */
	uint32_t active_mask;
	char *name;
	enum motionsensor_chip chip;
	enum motionsensor_type type;
	enum motionsensor_location location;
	const struct accelgyro_drv *drv;
	/* One mutex per physical chip. */
	struct mutex *mutex;
	void *drv_data;
	/* Only valid if flags & MOTIONSENSE_FLAG_INT_SIGNAL is true. */
	enum gpio_signal int_signal;
	/* Data used for online calibraiton, must match the sensor type. */
	struct online_calib_data
		online_calib_data[__cfg_select(CONFIG_ONLINE_CALIB, 1, 0)];

	/* i2c port */
	uint8_t port;
	/* i2c address or SPI slave logic GPIO. */
	uint16_t i2c_spi_addr_flags;

	/*
	 * Various flags, see MOTIONSENSE_FLAG_*
	 */
	uint32_t flags;

	const mat33_fp_t *rot_standard_ref;

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
	intv3_t raw_xyz;
	intv3_t xyz;
	intv3_t spoof_xyz;

	/* How many flush events are pending */
	uint32_t flush_pending;

	/*
	 * Allow EC to request an higher frequency for the sensors than the AP.
	 * We will downsample according to oversampling_ratio, or ignore the
	 * samples altogether if oversampling_ratio is 0.
	 */
	uint16_t oversampling;
	uint16_t oversampling_ratio;

	/*
	 * How many vector events are lost in the FIFO since last time
	 * FIFO info has been transmitted.
	 */
	uint16_t lost;

	/*
	 * For sensors in forced mode the ideal time to collect the next
	 * measurement.
	 *
	 * This is unused with sensors that interrupt the ec like hw fifo chips.
	 */
	uint32_t next_collection;

	/*
	 * The time in us between collection measurements
	 */
	uint32_t collection_rate;

	/* Minimum supported sampling frequency in miliHertz for this sensor */
	uint32_t min_frequency;

	/* Maximum supported sampling frequency in miliHertz for this sensor */
	uint32_t max_frequency;
};

/*
 * Mutex to protect sensor values between host command task and
 * motion sense task:
 * When we process CMD_DUMP, we want to be sure the motion sense
 * task is not updating the sensor values at the same time.
 */
extern struct mutex g_sensor_mutex;

/* Defined at board level. */
extern struct motion_sensor_t motion_sensors[];

#ifdef CONFIG_DYNAMIC_MOTION_SENSOR_COUNT
extern unsigned motion_sensor_count;
#else
extern const unsigned motion_sensor_count;
#endif
#if (!defined HAS_TASK_ALS) && (defined CONFIG_ALS)
/* Needed if reading ALS via LPC is needed */
extern const struct motion_sensor_t *motion_als_sensors[];
#endif

/* optionally defined at board level */
extern unsigned int motion_min_interval;

/*
 * Priority of the motion sense resume/suspend hooks, to be sure associated
 * hooks are scheduled properly.
 */
#define MOTION_SENSE_HOOK_PRIO (HOOK_PRIO_DEFAULT)

/**
 * Take actions at end of sensor initialization:
 * - print init done status to console,
 * - set default range.
 *
 * @param sensor sensor which was just initialized
 */
int sensor_init_done(const struct motion_sensor_t *sensor);

/**
 * Board specific function that is called when a double_tap event is detected.
 *
 */
void sensor_board_proc_double_tap(void);

#ifdef CONFIG_ORIENTATION_SENSOR
enum motionsensor_orientation motion_sense_remap_orientation(
		const struct motion_sensor_t *s,
		enum motionsensor_orientation orientation);
#endif

/*
 * There are 4 variables that represent the number of sensors:
 * SENSOR_COUNT: The number of available motion sensors in board.
 * MAX_MOTION_SENSORS: Max number of sensors. This equals to SENSOR_COUNT
 *                     (+ 1 when activity sensor is available).
 * motion_sensor_count: The number of motion sensors using currently.
 * ALL_MOTION_SENSORS: motion_sensor_count (+ 1 when activity sensor is
 *                     available).
 */
#if defined(CONFIG_GESTURE_HOST_DETECTION) || defined(CONFIG_ORIENTATION_SENSOR)
/* Add an extra sensor. We may need to add more */
#define MOTION_SENSE_ACTIVITY_SENSOR_ID (motion_sensor_count)
#define ALL_MOTION_SENSORS (MOTION_SENSE_ACTIVITY_SENSOR_ID + 1)
#define MAX_MOTION_SENSORS (SENSOR_COUNT + 1)
#else
#define ALL_MOTION_SENSORS (motion_sensor_count)
#define MAX_MOTION_SENSORS (SENSOR_COUNT)
#endif

#ifdef CONFIG_ALS_LIGHTBAR_DIMMING
#ifdef TEST_BUILD
#define MOTION_SENSE_LUX 0
#else
#define MOTION_SENSE_LUX motion_sensors[CONFIG_ALS_LIGHTBAR_DIMMING].raw_xyz[0]
#endif
#endif

/*
 * helper functions for clamping raw i32 values,
 * each sensor driver should take care of overflow condition.
 */
static inline uint16_t ec_motion_sensor_clamp_u16(const int32_t value)
{
	return (uint16_t)MIN(MAX(value, 0), (int32_t)UINT16_MAX);
}
static inline void ec_motion_sensor_clamp_u16s(uint16_t *arr, const int32_t *v)
{
	arr[0] = ec_motion_sensor_clamp_u16(v[0]);
	arr[1] = ec_motion_sensor_clamp_u16(v[1]);
	arr[2] = ec_motion_sensor_clamp_u16(v[2]);
}

static inline int16_t ec_motion_sensor_clamp_i16(const int32_t value)
{
	return MIN(MAX(value, (int32_t)INT16_MIN), (int32_t)INT16_MAX);
}
static inline void ec_motion_sensor_clamp_i16s(int16_t *arr, const int32_t *v)
{
	arr[0] = ec_motion_sensor_clamp_i16(v[0]);
	arr[1] = ec_motion_sensor_clamp_i16(v[1]);
	arr[2] = ec_motion_sensor_clamp_i16(v[2]);
}

/* direct assignment */
static inline void ec_motion_sensor_fill_values(
		struct ec_response_motion_sensor_data *dst, const int32_t *v)
{
	dst->data[0] = v[0];
	dst->data[1] = v[1];
	dst->data[2] = v[2];
}

#endif /* __CROS_EC_MOTION_SENSE_H */
