/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Motion sense module to read from various motion sensors. */

#include "accelgyro.h"
#include "atomic.h"
#include "body_detection.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gesture.h"
#include "hooks.h"
#include "host_command.h"
#include "hwtimer.h"
#include "lid_angle.h"
#include "lightbar.h"
#include "math_util.h"
#include "mkbp_event.h"
#include "motion_sense.h"
#include "motion_sense_fifo.h"
#include "motion_lid.h"
#include "online_calibration.h"
#include "power.h"
#include "queue.h"
#include "tablet_mode.h"
#include "timer.h"
#include "task.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_MOTION_SENSE, outstr)
#define CPRINTS(format, args...) cprints(CC_MOTION_SENSE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_MOTION_SENSE, format, ## args)

#ifdef CONFIG_ORIENTATION_SENSOR
/*
 * Orientation mode vectors, must match sequential ordering of
 * known orientations from enum motionsensor_orientation
 */
const intv3_t orientation_modes[] = {
	[MOTIONSENSE_ORIENTATION_LANDSCAPE] = { 0, -1, 0 },
	[MOTIONSENSE_ORIENTATION_PORTRAIT] = { 1, 0, 0 },
	[MOTIONSENSE_ORIENTATION_UPSIDE_DOWN_PORTRAIT] = { -1, 0, 0 },
	[MOTIONSENSE_ORIENTATION_UPSIDE_DOWN_LANDSCAPE] = { 0, 1, 0 },
};
#endif

/* Delay between FIFO interruption. */
static unsigned int ap_event_interval;

/* Minimum time in between running motion sense task loop. */
unsigned int motion_min_interval = CONFIG_MOTION_MIN_SENSE_WAIT_TIME * MSEC;
#ifdef CONFIG_CMD_ACCEL_INFO
static int accel_disp;
#endif

#define SENSOR_ACTIVE(_sensor) (sensor_active & (_sensor)->active_mask)

/*
 * Adjustment in us to ec rate when calculating interrupt interval:
 * To be sure the EC will send an interrupt even if it finishes processing
 * events slightly earlier than the previous period.
 */
#define MOTION_SENSOR_INT_ADJUSTMENT_US 10

struct mutex g_sensor_mutex;

/*
 * Current power level (S0, S3, S5, ...)
 */
test_export_static enum chipset_state_mask sensor_active;

#ifdef CONFIG_ACCEL_SPOOF_MODE
static void print_spoof_mode_status(int id);
#endif /* defined(CONFIG_ACCEL_SPOOF_MODE) */

/* Flags to control whether to send an ODR change event for a sensor */
static uint32_t odr_event_required;

/* Whether or not the FIFO interrupt should be enabled (set from the AP). */
__maybe_unused static int fifo_int_enabled;

static inline int motion_sensor_in_forced_mode(
		const struct motion_sensor_t *sensor)
{
#ifdef CONFIG_ACCEL_FORCE_MODE_MASK
	/* Sensor not in force mode, its irq_handler is getting data. */
	if (!(CONFIG_ACCEL_FORCE_MODE_MASK & (1 << (sensor - motion_sensors))))
		return 0;
	else
		return 1;
#else
	return 0;
#endif
}

/* Minimal amount of time since last collection before triggering a new one */
static inline int motion_sensor_time_to_read(const timestamp_t *ts,
		const struct motion_sensor_t *sensor)
{
	if (sensor->collection_rate == 0)
		return 0;

	/*
	 * If the time is within the min motion interval (3 ms) go ahead and
	 * read from the sensor
	 */
	return time_after(ts->le.lo,
			  sensor->next_collection - motion_min_interval);
}

static enum sensor_config motion_sense_get_ec_config(void)
{
	switch (sensor_active) {
	case SENSOR_ACTIVE_S0:
		return SENSOR_CONFIG_EC_S0;
	case SENSOR_ACTIVE_S3:
		return SENSOR_CONFIG_EC_S3;
	case SENSOR_ACTIVE_S5:
		return SENSOR_CONFIG_EC_S5;
	default:
		CPRINTS("get_ec_config: Invalid active state: %x",
			sensor_active);
		return SENSOR_CONFIG_MAX;
	}
}
/* motion_sense_set_data_rate
 *
 * Set the sensor data rate. It is altered when the AP change the data
 * rate or when the power state changes.
 */
int motion_sense_set_data_rate(struct motion_sensor_t *sensor)
{
	int roundup, ap_odr_mhz = 0, ec_odr_mhz, odr, ret;
	enum sensor_config config_id;
	timestamp_t ts = get_time();

	/* We assume the sensor is initialized */

	/* Check the AP setting first. */
	if (sensor_active != SENSOR_ACTIVE_S5)
		ap_odr_mhz = BASE_ODR(sensor->config[SENSOR_CONFIG_AP].odr);

	/* check if the EC set the sensor ODR at a higher frequency */
	config_id = motion_sense_get_ec_config();
	ec_odr_mhz = BASE_ODR(sensor->config[config_id].odr);
	if (ec_odr_mhz > ap_odr_mhz) {
		odr = ec_odr_mhz;
	} else {
		odr = ap_odr_mhz;
		config_id = SENSOR_CONFIG_AP;
	}
	roundup = !!(sensor->config[config_id].odr & ROUND_UP_FLAG);

	ret = sensor->drv->set_data_rate(sensor, odr, roundup);
	if (ret)
		return ret;

#ifdef CONFIG_CONSOLE_VERBOSE
	CPRINTS("%s ODR: %d - roundup %d from config %d [AP %d]",
		sensor->name, odr, roundup, config_id,
		BASE_ODR(sensor->config[SENSOR_CONFIG_AP].odr));
#else
	CPRINTS("%c%d ODR %d rup %d cfg %d AP %d",
		sensor->name[0], sensor->type, odr, roundup, config_id,
		BASE_ODR(sensor->config[SENSOR_CONFIG_AP].odr));
#endif
	mutex_lock(&g_sensor_mutex);
	if (ap_odr_mhz)
		/*
		 * In case the AP want to run the sensors faster than it can,
		 * be sure we don't see the ratio to 0.
		 */
		sensor->oversampling_ratio = MAX(1,
			sensor->drv->get_data_rate(sensor) / ap_odr_mhz);
	else
		sensor->oversampling_ratio = 0;

	/*
	 * Reset last collection: the last collection may be so much in the past
	 * it may appear to be in the future.
	 */
	odr = sensor->drv->get_data_rate(sensor);
	sensor->collection_rate = odr > 0 ? SECOND * 1000 / odr : 0;
	sensor->next_collection = ts.le.lo + sensor->collection_rate;
	sensor->oversampling = 0;
	mutex_unlock(&g_sensor_mutex);
#ifdef CONFIG_BODY_DETECTION
	if (sensor - motion_sensors == CONFIG_BODY_DETECTION_SENSOR)
		body_detect_reset();
#endif
	return 0;
}

static int motion_sense_set_ec_rate_from_ap(
		const struct motion_sensor_t *sensor,
		unsigned int new_rate_us)
{
	int odr_mhz = sensor->drv->get_data_rate(sensor);

	if (new_rate_us == 0)
		return 0;
	if (motion_sensor_in_forced_mode(sensor))
		/*
		 * AP EC sampling rate does not matter: we will collect at the
		 * requested sensor frequency.
		 */
		goto end_set_ec_rate_from_ap;
	if (odr_mhz == 0)
		goto end_set_ec_rate_from_ap;

	/*
	 * If the EC collection rate is close to the sensor data rate,
	 * given variation from the EC scheduler, it is possible that a sensor
	 * will not present any measurement for a given time slice, and then 2
	 * measurement for the next. That will create a large interval between
	 * 2 measurements.
	 * To prevent that, increase the EC period by 5% to be sure to get at
	 * least one measurement at every collection time.
	 * We will apply that correction only if the ec rate is within 10% of
	 * the data rate.
	 */
	if (SECOND * 1100 / odr_mhz > new_rate_us)
		new_rate_us = new_rate_us / 100 * 105;

end_set_ec_rate_from_ap:
	return MAX(new_rate_us, motion_min_interval);
}


/*
 * motion_sense_select_ec_rate
 *
 * Calculate the ec_rate for a given sensor.
 * - sensor: sensor to use
 * - config_id: determine the requester (AP or EC).
 * - interrupt:
 * If interrupt is set: return the sampling rate requested by AP or EC.
 * If interrupt is not set and the sensor is in forced mode,
 * we return the rate needed to probe the sensor at the right ODR.
 * otherwise return the sampling rate requested by AP or EC.
 *
 * return rate in us.
 */
static int motion_sense_select_ec_rate(
		const struct motion_sensor_t *sensor,
		enum sensor_config config_id,
		int interrupt)
{
	if (interrupt == 0 && motion_sensor_in_forced_mode(sensor)) {
		int rate_mhz = BASE_ODR(sensor->config[config_id].odr);
		/* we have to run ec at the sensor frequency rate.*/
		if (rate_mhz > 0)
			return SECOND * 1000 / rate_mhz;
		else
			return 0;
	} else {
		return sensor->config[config_id].ec_rate;
	}
}

/* motion_sense_ec_rate
 *
 * Calculate the sensor ec rate. It will be use to set the motion task polling
 * rate.
 *
 * Return the EC rate, in us.
 */
static int motion_sense_ec_rate(struct motion_sensor_t *sensor)
{
	int ec_rate = 0, ec_rate_from_cfg;

	/* Check the AP setting first. */
	if (sensor_active != SENSOR_ACTIVE_S5)
		ec_rate = motion_sense_select_ec_rate(
			sensor, SENSOR_CONFIG_AP, 0);

	ec_rate_from_cfg = motion_sense_select_ec_rate(
			sensor, motion_sense_get_ec_config(), 0);

	if (ec_rate_from_cfg != 0)
		if (ec_rate == 0 || ec_rate_from_cfg < ec_rate)
			ec_rate = ec_rate_from_cfg;
	return ec_rate;
}

/*
 * motion_sense_set_motion_intervals
 *
 * Set the wake up interval for the motion sense thread.
 * It is set to the highest frequency one of the sensors need to be polled at.
 *
 * Note: Not static to be tested.
 */
static void motion_sense_set_motion_intervals(void)
{
	int i, sensor_ec_rate, ec_int_rate = 0;
	struct motion_sensor_t *sensor;
	for (i = 0; i < motion_sensor_count; ++i) {
		sensor = &motion_sensors[i];
		/*
		 * If the sensor is sleeping, no need to check it periodically.
		 */
		if ((sensor->state != SENSOR_INITIALIZED) ||
		    (sensor->drv->get_data_rate(sensor) == 0))
			continue;

		sensor_ec_rate = motion_sense_select_ec_rate(
				sensor, SENSOR_CONFIG_AP, 1);
		if (ec_int_rate == 0 ||
		    (sensor_ec_rate && sensor_ec_rate < ec_int_rate))
			ec_int_rate = sensor_ec_rate;
	}

	ap_event_interval =
		MAX(0, ec_int_rate - MOTION_SENSOR_INT_ADJUSTMENT_US);
	/*
	 * Wake up the motion sense task: we want to sensor task to take
	 * in account the new period right away.
	 */
	task_wake(TASK_ID_MOTIONSENSE);
}

static inline int motion_sense_init(struct motion_sensor_t *sensor)
{
	int ret, cnt = 3;

	BUILD_ASSERT(SENSOR_COUNT < 32);

	/* Initialize accelerometers. */
	do {
		ret = sensor->drv->init(sensor);
	} while ((ret != EC_SUCCESS) && (--cnt > 0));

	if (ret != EC_SUCCESS) {
		sensor->state = SENSOR_INIT_ERROR;
	} else {
		sensor->state = SENSOR_INITIALIZED;
		motion_sense_set_data_rate(sensor);
	}

	return ret;
}

/*
 * sensor_init_done
 *
 * Called by init routine of each sensors when successful.
 */
int sensor_init_done(const struct motion_sensor_t *s)
{
	int ret;

	ret = s->drv->set_range(s, BASE_RANGE(s->default_range),
				!!(s->default_range & ROUND_UP_FLAG));
	if (ret == EC_RES_SUCCESS) {
#ifdef CONFIG_CONSOLE_VERBOSE
		CPRINTS("%s: MS Done Init type:0x%X range:%d",
				s->name, s->type, s->drv->get_range(s));
#else
		CPRINTS("%c%d InitDone r:%d", s->name[0], s->type,
				s->drv->get_range(s));
#endif
	}
	return ret;
}
/*
 * motion_sense_switch_sensor_rate
 *
 * Suspend all sensors that are not needed.
 * Mark them as uninitialized, they will lose power and
 * need to be initialized again.
 */
static void motion_sense_switch_sensor_rate(void)
{
	int i, ret;
	struct motion_sensor_t *sensor;
	for (i = 0; i < motion_sensor_count; ++i) {
		sensor = &motion_sensors[i];
		if (SENSOR_ACTIVE(sensor)) {
			/* Initialize or just back the odr previously set. */
			if (sensor->state == SENSOR_INITIALIZED) {
				motion_sense_set_data_rate(sensor);
			} else {
				ret = motion_sense_init(sensor);
				if (ret != EC_SUCCESS) {
					CPRINTS("%s: %d: init failed: %d",
						sensor->name, i, ret);
#if defined(CONFIG_TABLET_MODE) && defined(CONFIG_LID_ANGLE)
					/*
					 * No tablet mode allowed if an accel
					 * is not working.
					 */
					if (i == CONFIG_LID_ANGLE_SENSOR_BASE ||
					    i == CONFIG_LID_ANGLE_SENSOR_LID) {
						tablet_set_mode(0);
					}
#endif
				}
			}
		} else {
			/* The sensors are being powered off */
			if (sensor->state == SENSOR_INITIALIZED)
				sensor->state = SENSOR_NOT_INITIALIZED;
		}
	}
	motion_sense_set_motion_intervals();
}
DECLARE_DEFERRED(motion_sense_switch_sensor_rate);

static void motion_sense_shutdown(void)
{
	int i;
	struct motion_sensor_t *sensor;
#ifdef CONFIG_GESTURE_DETECTION_MASK
	uint32_t enabled = 0, disabled, mask;
#endif

	sensor_active = SENSOR_ACTIVE_S5;
	for (i = 0; i < motion_sensor_count; i++) {
		sensor = &motion_sensors[i];
		/* Forget about changes made by the AP */
		sensor->config[SENSOR_CONFIG_AP].odr = 0;
		sensor->config[SENSOR_CONFIG_AP].ec_rate = 0;
	}
	motion_sense_switch_sensor_rate();

	/* Forget activities set by the AP */
#ifdef CONFIG_GESTURE_DETECTION_MASK
	mask = CONFIG_GESTURE_DETECTION_MASK;
	while (mask) {
		i = get_next_bit(&mask);
		sensor = &motion_sensors[i];
		if (sensor->state != SENSOR_INITIALIZED)
			continue;
		sensor->drv->list_activities(sensor,
				&enabled, &disabled);
		/* exclude double tap, it is used internally. */
		enabled &= ~BIT(MOTIONSENSE_ACTIVITY_DOUBLE_TAP);
		while (enabled) {
			int activity = get_next_bit(&enabled);
			sensor->drv->manage_activity(sensor, activity, 0, NULL);
		}
		/* Re-enable double tap in case AP disabled it */
		sensor->drv->manage_activity(sensor,
				MOTIONSENSE_ACTIVITY_DOUBLE_TAP, 1, NULL);
	}
#endif
#ifdef CONFIG_BODY_DETECTION
	/* disable the body detection since motion sensor is down */
	body_detect_set_enable(false);
#endif
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, motion_sense_shutdown,
	     MOTION_SENSE_HOOK_PRIO);

static void motion_sense_suspend(void)
{
	/*
	 *  If we are coming from S5, don't enter suspend:
	 *  We will go in SO almost immediately.
	 */
	if (sensor_active == SENSOR_ACTIVE_S5)
		return;

	sensor_active = SENSOR_ACTIVE_S3;

#ifdef CONFIG_BODY_DETECTION
	/* disable the body detection since motion sensor is suspended */
	body_detect_set_enable(false);
#endif
	/*
	 * During shutdown sequence sensor rails can be powered down
	 * asynchronously to the EC hence EC cannot interlock the sensor
	 * states with the power down states. To avoid this issue, defer
	 * switching the sensors rate with a configurable delay if in S3.
	 * By the time deferred function is serviced, if the chipset is
	 * in S5 we can back out from switching the sensor rate.
	 *
	 * TODO: This does not fix the issue completely. It is mitigating
	 * some of the accesses when we're going from S0->S5 with a very
	 * brief stop in S3.
	 */
	hook_call_deferred(&motion_sense_switch_sensor_rate_data,
			   CONFIG_MOTION_SENSE_SUSPEND_DELAY_US);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, motion_sense_suspend,
	     MOTION_SENSE_HOOK_PRIO);

static void motion_sense_resume(void)
{
	sensor_active = SENSOR_ACTIVE_S0;
	hook_call_deferred(&motion_sense_switch_sensor_rate_data,
			   CONFIG_MOTION_SENSE_RESUME_DELAY_US);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, motion_sense_resume,
	     MOTION_SENSE_HOOK_PRIO);

static void motion_sense_startup(void)
{
	/*
	 * If the AP is already in S0, call the resume hook now.
	 * We may initialize the sensor 2 times (once in RO, another time in
	 * RW), but it may be necessary if the init sequence has changed.
	 */
	if (chipset_in_state(SENSOR_ACTIVE_S0_S3_S5))
		motion_sense_shutdown();
	if (chipset_in_state(SENSOR_ACTIVE_S0_S3))
		motion_sense_suspend();
	if (chipset_in_state(SENSOR_ACTIVE_S0))
		motion_sense_resume();
}
DECLARE_HOOK(HOOK_INIT, motion_sense_startup,
	     MOTION_SENSE_HOOK_PRIO);

/* Write to LPC status byte to represent that accelerometers are present. */
static inline void set_present(uint8_t *lpc_status)
{
	*lpc_status |= EC_MEMMAP_ACC_STATUS_PRESENCE_BIT;
}

#ifdef CONFIG_MOTION_FILL_LPC_SENSE_DATA
/* Update/Write LPC data */
static inline void update_sense_data(uint8_t *lpc_status, int *psample_id)
{
	int s, d, i;
	int16_t *lpc_data = (int16_t *)host_get_memmap(EC_MEMMAP_ACC_DATA);
#if (!defined HAS_TASK_ALS) && (defined CONFIG_ALS)
	uint16_t *lpc_als = (uint16_t *)host_get_memmap(EC_MEMMAP_ALS);
#endif
	struct motion_sensor_t *sensor;
	/*
	 * Set the busy bit before writing the sensor data. Increment
	 * the counter and clear the busy bit after writing the sensor
	 * data. On the host side, the host needs to make sure the busy
	 * bit is not set and that the counter remains the same before
	 * and after reading the data.
	 */
	*lpc_status |= EC_MEMMAP_ACC_STATUS_BUSY_BIT;

	/*
	 * Copy sensor data to shared memory. Note that this code
	 * assumes little endian, which is what the host expects. Also,
	 * note that we share the lid angle calculation with host only
	 * for debugging purposes. The EC lid angle is an approximation
	 * with uncalibrated accelerometers. The AP calculates a separate,
	 * more accurate lid angle.
	 */
#ifdef CONFIG_LID_ANGLE
	lpc_data[0] = motion_lid_get_angle();
#else
	lpc_data[0] = LID_ANGLE_UNRELIABLE;
#endif
	/*
	 * The first 2 entries must be accelerometers, then gyroscope.
	 * If there is only one accel and one gyro, the entry for the second
	 * accel is skipped.
	 */
	for (s = 0, d = 0; d < 3 && s < motion_sensor_count; s++, d++) {
		sensor = &motion_sensors[s];
		if (sensor->type > MOTIONSENSE_TYPE_GYRO)
			break;
		else if (sensor->type == MOTIONSENSE_TYPE_GYRO)
			d = 2;

		for (i = X; i <= Z; i++)
			lpc_data[1 + i + 3 * d] =
				ec_motion_sensor_clamp_i16(sensor->xyz[i]);
	}

#if (!defined HAS_TASK_ALS) && (defined CONFIG_ALS)
	for (i = 0; i < EC_ALS_ENTRIES && i < ALS_COUNT; i++)
		lpc_als[i] = ec_motion_sensor_clamp_u16(
				motion_als_sensors[i]->xyz[X]);
#endif

	/*
	 * Increment sample id and clear busy bit to signal we finished
	 * updating data.
	 */
	*psample_id = (*psample_id + 1) &
			EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK;
	*lpc_status = EC_MEMMAP_ACC_STATUS_PRESENCE_BIT | *psample_id;
}
#endif

static int motion_sense_read(struct motion_sensor_t *sensor)
{
	if (sensor->state != SENSOR_INITIALIZED)
		return EC_ERROR_UNKNOWN;

	if (sensor->drv->get_data_rate(sensor) == 0)
		return EC_ERROR_NOT_POWERED;

#ifdef CONFIG_ACCEL_SPOOF_MODE
	/*
	 * If the sensor is in spoof mode, the readings are already present in
	 * spoof_xyz.
	 */
	if (sensor->flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE)
		return EC_SUCCESS;
#endif /* defined(CONFIG_ACCEL_SPOOF_MODE) */

	/* Otherwise, read all raw X,Y,Z accelerations. */
	return sensor->drv->read(sensor, sensor->raw_xyz);
}


static inline void increment_sensor_collection(struct motion_sensor_t *sensor,
					       const timestamp_t *ts)
{
	sensor->next_collection += sensor->collection_rate;

	if (time_after(ts->le.lo, sensor->next_collection)) {
		/*
		 * If we get here it means that we completely missed a sensor
		 * collection time and we attempt to recover by scheduling as
		 * soon as possible. This should not happen and if it does it
		 * means that the ec cannot handle the requested data rate.
		 */
		int missed_events =
			time_until(sensor->next_collection, ts->le.lo) /
			sensor->collection_rate;

		CPRINTS("%s Missed %d data collections at %u - rate: %d",
			sensor->name, missed_events, sensor->next_collection,
			sensor->collection_rate);
		sensor->next_collection = ts->le.lo + motion_min_interval;
	}
}

/**
 * Commit the data in a sensor's raw_xyz vector. This operation might have
 * different meanings depending on the CONFIG_ACCEL_FIFO flag.
 *
 * @param s Pointer to the sensor.
 */
static void motion_sense_push_raw_xyz(struct motion_sensor_t *s)
{
	if (IS_ENABLED(CONFIG_ACCEL_FIFO)) {
		struct ec_response_motion_sensor_data vector;
		int *v = s->raw_xyz;

		vector.flags = 0;
		vector.sensor_num = s - motion_sensors;
		if (IS_ENABLED(CONFIG_ACCEL_SPOOF_MODE) &&
		    s->flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE)
			v = s->spoof_xyz;

		mutex_lock(&g_sensor_mutex);

		ec_motion_sensor_fill_values(&vector, v);

		mutex_unlock(&g_sensor_mutex);

		motion_sense_fifo_stage_data(&vector, s, 3,
					     __hw_clock_source_read());
		motion_sense_fifo_commit_data();
	} else {
		mutex_lock(&g_sensor_mutex);
		memcpy(s->xyz, s->raw_xyz, sizeof(s->xyz));
		mutex_unlock(&g_sensor_mutex);
	}
}

static int motion_sense_process(struct motion_sensor_t *sensor,
				uint32_t *event,
				const timestamp_t *ts)
{
	int ret = EC_SUCCESS;
	int is_odr_pending = 0;
	int has_data_read = 0;
	int sensor_num = sensor - motion_sensors;

	if (*event & TASK_EVENT_MOTION_ODR_CHANGE) {
		const int sensor_bit = 1 << sensor_num;
		int odr_pending =
			deprecated_atomic_read_clear(&odr_event_required);

		is_odr_pending = odr_pending & sensor_bit;
		odr_pending &= ~sensor_bit;
		deprecated_atomic_or(&odr_event_required, odr_pending);
	}

#ifdef CONFIG_ACCEL_INTERRUPTS
	if ((*event & TASK_EVENT_MOTION_INTERRUPT_MASK || is_odr_pending) &&
	    (sensor->drv->irq_handler != NULL)) {
		ret = sensor->drv->irq_handler(sensor, event);
		if (ret == EC_SUCCESS)
			has_data_read = 1;
	}
#endif /* CONFIG_ACCEL_INTERRUPTS */
	if (motion_sensor_in_forced_mode(sensor)) {
		if (motion_sensor_time_to_read(ts, sensor)) {
			ret = motion_sense_read(sensor);
			increment_sensor_collection(sensor, ts);
		} else {
			ret = EC_ERROR_BUSY;
		}

		if (ret == EC_SUCCESS) {
			motion_sense_push_raw_xyz(sensor);
			has_data_read = 1;
		}
	}
	if (IS_ENABLED(CONFIG_ACCEL_FIFO) &&
	    *event & TASK_EVENT_MOTION_FLUSH_PENDING) {
		int flush_pending =
			deprecated_atomic_read_clear(&sensor->flush_pending);

		for (; flush_pending > 0; flush_pending--) {
			motion_sense_fifo_insert_async_event(
				sensor, ASYNC_EVENT_FLUSH);
		}
	}

	/* ODR change was requested. */
	if (is_odr_pending) {
		motion_sense_set_data_rate(sensor);
		motion_sense_set_motion_intervals();
		if (IS_ENABLED(CONFIG_ACCEL_FIFO))
			motion_sense_fifo_insert_async_event(
				sensor, ASYNC_EVENT_ODR);
	}
	if (has_data_read) {
#ifdef CONFIG_GESTURE_SW_DETECTION
		/* Run gesture recognition engine */
		if (sensor_num == CONFIG_GESTURE_SENSOR_DOUBLE_TAP)
			gesture_calc(event);
#endif
#ifdef CONFIG_BODY_DETECTION
		if (sensor_num == CONFIG_BODY_DETECTION_SENSOR)
			body_detect();
#endif
	}
	return ret;
}

#ifdef CONFIG_ORIENTATION_SENSOR
enum motionsensor_orientation motion_sense_remap_orientation(
		const struct motion_sensor_t *s,
		enum motionsensor_orientation orientation)
{
	enum motionsensor_orientation rotated_orientation;
	const intv3_t *orientation_v;
	intv3_t rotated_orientation_v;

	if (orientation == MOTIONSENSE_ORIENTATION_UNKNOWN)
		return MOTIONSENSE_ORIENTATION_UNKNOWN;

	orientation_v = &orientation_modes[orientation];
	rotate(*orientation_v, *s->rot_standard_ref, rotated_orientation_v);
	rotated_orientation = ((2 * rotated_orientation_v[1] +
			rotated_orientation_v[0] + 4) % 5);
	return rotated_orientation;
}
#endif

#ifdef CONFIG_GESTURE_DETECTION
static void check_and_queue_gestures(uint32_t *event)
{
#ifdef CONFIG_ORIENTATION_SENSOR
	const struct motion_sensor_t *sensor;
#endif

#ifdef CONFIG_GESTURE_SENSOR_DOUBLE_TAP
	if (*event & TASK_EVENT_MOTION_ACTIVITY_INTERRUPT(
				MOTIONSENSE_ACTIVITY_DOUBLE_TAP)) {
#ifdef CONFIG_GESTURE_HOST_DETECTION
		struct ec_response_motion_sensor_data vector;

		/*
		 * Send events to the FIFO
		 * AP is ignoring double tap event, do no wake up and no
		 * automatic disable.
		 */
#ifdef CONFIG_GESTURE_SENSOR_DOUBLE_TAP_FOR_HOST
		vector.flags = MOTIONSENSE_SENSOR_FLAG_WAKEUP;
#else
		vector.flags = 0;
#endif
		vector.activity = MOTIONSENSE_ACTIVITY_DOUBLE_TAP;
		vector.state = 1; /* triggered */
		vector.sensor_num = MOTION_SENSE_ACTIVITY_SENSOR_ID;
		motion_sense_fifo_stage_data(&vector, NULL, 0,
					     __hw_clock_source_read());
		motion_sense_fifo_commit_data();
#endif
		/* Call board specific function to process tap */
		sensor_board_proc_double_tap();
	}
#endif
#ifdef CONFIG_GESTURE_SIGMO
	if (*event & TASK_EVENT_MOTION_ACTIVITY_INTERRUPT(
				MOTIONSENSE_ACTIVITY_SIG_MOTION)) {
		struct motion_sensor_t *activity_sensor;
#ifdef CONFIG_GESTURE_HOST_DETECTION
		struct ec_response_motion_sensor_data vector;

		/* Send events to the FIFO */
		vector.flags = MOTIONSENSE_SENSOR_FLAG_WAKEUP;
		vector.activity = MOTIONSENSE_ACTIVITY_SIG_MOTION;
		vector.state = 1; /* triggered */
		vector.sensor_num = MOTION_SENSE_ACTIVITY_SENSOR_ID;
		motion_sense_fifo_stage_data(&vector, NULL, 0,
				__hw_clock_source_read());
		motion_sense_fifo_commit_data();
#endif
		/* Disable further detection */
		activity_sensor = &motion_sensors[CONFIG_GESTURE_SIGMO];
		activity_sensor->drv->manage_activity(
				activity_sensor,
				MOTIONSENSE_ACTIVITY_SIG_MOTION,
				0, NULL);
	}
#endif

#ifdef CONFIG_ORIENTATION_SENSOR
	sensor = &motion_sensors[LID_ACCEL];
	if (SENSOR_ACTIVE(sensor) && (sensor->state == SENSOR_INITIALIZED)) {
		struct ec_response_motion_sensor_data vector = {
			.flags = 0,
			.activity = MOTIONSENSE_ACTIVITY_ORIENTATION,
			.sensor_num = MOTION_SENSE_ACTIVITY_SENSOR_ID,
		};

		mutex_lock(sensor->mutex);
		if (ORIENTATION_CHANGED(sensor) && (GET_ORIENTATION(sensor) !=
				MOTIONSENSE_ORIENTATION_UNKNOWN)) {
			SET_ORIENTATION_UPDATED(sensor);
			vector.state = GET_ORIENTATION(sensor);
			motion_sense_fifo_add_data(&vector, NULL, 0,
						   __hw_clock_source_read());
#ifdef CONFIG_DEBUG_ORIENTATION
			{
				static const char * const mode_strs[] = {
						"Landscape",
						"Portrait",
						"Inv_Portrait",
						"Inv_Landscape",
						"Unknown"
				};
				CPRINTS(mode_strs[GET_ORIENTATION(sensor)]);
			}
#endif
		}
		mutex_unlock(sensor->mutex);
	}
#endif
}
#endif

/*
 * Motion Sense Task
 * Requirement: motion_sensors[] are defined in board.c file.
 * Two (minimum) Accelerometers:
 *    1 in the A/B(lid, display) and 1 in the C/D(base, keyboard)
 * Gyro Sensor (optional)
 */
void motion_sense_task(void *u)
{
	int i, ret, wait_us;
	timestamp_t ts_begin_task, ts_end_task;
	int32_t time_diff;
	uint32_t event = 0;
	uint16_t ready_status = 0;
	struct motion_sensor_t *sensor;
#ifdef CONFIG_LID_ANGLE
	const uint16_t lid_angle_sensors = (BIT(CONFIG_LID_ANGLE_SENSOR_BASE)|
					    BIT(CONFIG_LID_ANGLE_SENSOR_LID));
#endif
	timestamp_t ts_last_int;
#ifdef CONFIG_MOTION_FILL_LPC_SENSE_DATA
	int sample_id = 0;
	uint8_t *lpc_status;

	lpc_status = host_get_memmap(EC_MEMMAP_ACC_STATUS);
	set_present(lpc_status);
#endif

	if (IS_ENABLED(CONFIG_ACCEL_FIFO)) {
		motion_sense_fifo_init();
		ts_last_int = get_time();
	}

	while (1) {
		ts_begin_task = get_time();
		for (i = 0; i < motion_sensor_count; ++i) {

			sensor = &motion_sensors[i];

			/* if the sensor is active in the current power state */
			if (SENSOR_ACTIVE(sensor)) {
				if (sensor->state != SENSOR_INITIALIZED) {
					continue;
				}

				ret = motion_sense_process(sensor, &event,
						&ts_begin_task);
				if (ret != EC_SUCCESS)
					continue;
				ready_status |= BIT(i);
			}
		}
#ifdef CONFIG_GESTURE_DETECTION
		check_and_queue_gestures(&event);
#endif
#ifdef CONFIG_LID_ANGLE
		/*
		 * Check to see that the sensors required for lid angle
		 * calculation are ready.
		 */
		ready_status &= lid_angle_sensors;
		if (ready_status == lid_angle_sensors) {
			motion_lid_calc();
			ready_status = 0;
		}
#endif
#ifdef CONFIG_CMD_ACCEL_INFO
		if (accel_disp) {
			CPRINTF("[%pT event 0x%08x ",
				PRINTF_TIMESTAMP_NOW, event);
			for (i = 0; i < motion_sensor_count; ++i) {
				sensor = &motion_sensors[i];
				CPRINTF("%s=%-5d, %-5d, %-5d ", sensor->name,
					sensor->xyz[X],
					sensor->xyz[Y],
					sensor->xyz[Z]);
			}
#ifdef CONFIG_LID_ANGLE
			CPRINTF("a=%-4d", motion_lid_get_angle());
#endif
			CPRINTF("]\n");
		}
#endif
#ifdef CONFIG_MOTION_FILL_LPC_SENSE_DATA
		update_sense_data(lpc_status, &sample_id);
#endif

		/*
		 * Ask the host to flush the queue if
		 * - a flush event has been queued.
		 * - the queue is almost full,
		 * - we haven't done it for a while.
		 */
		if (IS_ENABLED(CONFIG_ACCEL_FIFO) &&
		    (motion_sense_fifo_wake_up_needed() ||
		     event & (TASK_EVENT_MOTION_ODR_CHANGE |
			      TASK_EVENT_MOTION_FLUSH_PENDING) ||
		     motion_sense_fifo_over_thres() ||
		     (ap_event_interval > 0 &&
		      time_after(ts_begin_task.le.lo,
				 ts_last_int.le.lo + ap_event_interval)))) {
			if ((event & TASK_EVENT_MOTION_FLUSH_PENDING) == 0) {
				motion_sense_fifo_add_timestamp(
					__hw_clock_source_read());
			}
			ts_last_int = ts_begin_task;
#ifdef CONFIG_MKBP_EVENT
			/*
			 * Send an event if we know we are in S0 and the kernel
			 * driver is listening, or the AP needs to be waken up.
			 * In the latter case, the driver pulls the event and
			 * will resume listening until it is suspended again.
			 */
			if ((fifo_int_enabled &&
			     sensor_active == SENSOR_ACTIVE_S0) ||
			    motion_sense_fifo_wake_up_needed()) {
				mkbp_send_event(EC_MKBP_EVENT_SENSOR_FIFO);
				motion_sense_fifo_reset_wake_up_needed();
			}
#endif /* CONFIG_MKBP_EVENT */
		}

		ts_end_task = get_time();
		wait_us = -1;

		for (i = 0; i < motion_sensor_count; i++) {
			struct motion_sensor_t *sensor = &motion_sensors[i];

			if (!motion_sensor_in_forced_mode(sensor) ||
			   sensor->collection_rate == 0)
				continue;

			time_diff = time_until(ts_end_task.le.lo,
					       sensor->next_collection);

			/* We missed our collection time so wake soon */
			if (time_diff <= 0) {
				wait_us = 0;
				break;
			}

			if (wait_us == -1 || wait_us > time_diff)
				wait_us = time_diff;
		}

		if (wait_us >= 0 && wait_us < motion_min_interval) {
			/*
			* Guarantee some minimum delay to allow other lower
			* priority tasks to run.
			*/
			wait_us = motion_min_interval;
		}

		event = task_wait_event(wait_us);
	}
}

/*****************************************************************************/
/* Host commands */

/* Function to map host sensor IDs to motion sensor. */
static struct motion_sensor_t
	*host_sensor_id_to_real_sensor(int host_id)
{
	struct motion_sensor_t *sensor;

	if (host_id >= motion_sensor_count)
		return NULL;
	sensor = &motion_sensors[host_id];

	/* if sensor is powered and initialized, return match */
	if (SENSOR_ACTIVE(sensor) && (sensor->state == SENSOR_INITIALIZED))
		return sensor;

	/* If no match then the EC currently doesn't support ID received. */
	return NULL;
}

static struct motion_sensor_t
	*host_sensor_id_to_motion_sensor(int host_id)
{
#ifdef CONFIG_GESTURE_HOST_DETECTION
	if (host_id == MOTION_SENSE_ACTIVITY_SENSOR_ID)
		/*
		 * Return the info for the first sensor that
		 * support some gestures.
		 */
		return host_sensor_id_to_real_sensor(
			__builtin_ctz(CONFIG_GESTURE_DETECTION_MASK));
#endif
	return host_sensor_id_to_real_sensor(host_id);
}

static enum ec_status host_cmd_motion_sense(struct host_cmd_handler_args *args)
{
	const struct ec_params_motion_sense *in = args->params;
	struct ec_response_motion_sense *out = args->response;
	struct motion_sensor_t *sensor;
	int i, ret = EC_RES_INVALID_PARAM, reported;

	switch (in->cmd) {
	case MOTIONSENSE_CMD_DUMP:
		out->dump.module_flags =
			(*(host_get_memmap(EC_MEMMAP_ACC_STATUS)) &
			 EC_MEMMAP_ACC_STATUS_PRESENCE_BIT) ?
			MOTIONSENSE_MODULE_FLAG_ACTIVE : 0;
		out->dump.sensor_count = ALL_MOTION_SENSORS;
		args->response_size = sizeof(out->dump);
		reported = MIN(ALL_MOTION_SENSORS, in->dump.max_sensor_count);
		mutex_lock(&g_sensor_mutex);
		for (i = 0; i < reported; i++) {
			out->dump.sensor[i].flags =
				MOTIONSENSE_SENSOR_FLAG_PRESENT;
			if (i < motion_sensor_count) {
				sensor = &motion_sensors[i];
				ec_motion_sensor_fill_values(
					&out->dump.sensor[i], sensor->xyz);
			} else {
				memset(out->dump.sensor[i].data, 0,
				       3 * sizeof(int16_t));
			}
		}
		mutex_unlock(&g_sensor_mutex);
		args->response_size += reported *
			sizeof(struct ec_response_motion_sensor_data);
		break;

	case MOTIONSENSE_CMD_DATA:
		sensor = host_sensor_id_to_real_sensor(
				in->sensor_odr.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;

		out->data.flags = 0;

		mutex_lock(&g_sensor_mutex);
		ec_motion_sensor_fill_values(&out->data, sensor->xyz);
		mutex_unlock(&g_sensor_mutex);

		args->response_size = sizeof(out->data);
		break;

	case MOTIONSENSE_CMD_INFO:
		sensor = host_sensor_id_to_motion_sensor(
				in->sensor_odr.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;

#ifdef CONFIG_GESTURE_HOST_DETECTION
		if (in->sensor_odr.sensor_num ==
		    MOTION_SENSE_ACTIVITY_SENSOR_ID)
			out->info.type = MOTIONSENSE_TYPE_ACTIVITY;
		else
#endif
			out->info.type = sensor->type;

		out->info.location = sensor->location;
		out->info.chip = sensor->chip;
		if (args->version < 3)
			args->response_size = sizeof(out->info);
		if (args->version >= 3) {
			out->info_3.min_frequency = sensor->min_frequency;
			out->info_3.max_frequency = sensor->max_frequency;
			out->info_3.fifo_max_event_count = MAX_FIFO_EVENT_COUNT;
			args->response_size = sizeof(out->info_3);
		}
		if (args->version >= 4) {
			if (IS_ENABLED(CONFIG_ONLINE_CALIB) &&
			    sensor->drv->read_temp)
				out->info_4.flags |=
					MOTION_SENSE_CMD_INFO_FLAG_ONLINE_CALIB;
			args->response_size = sizeof(out->info_4);
		}
		break;

	case MOTIONSENSE_CMD_EC_RATE:
		sensor = host_sensor_id_to_real_sensor(
				in->sensor_odr.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;

		/*
		 * Set new sensor sampling rate when AP is on, if the data arg
		 * has a value.
		 */
		if (in->ec_rate.data != EC_MOTION_SENSE_NO_VALUE) {
			sensor->config[SENSOR_CONFIG_AP].ec_rate =
				motion_sense_set_ec_rate_from_ap(
					sensor, in->ec_rate.data * MSEC);
			/* Bound the new sampling rate. */
			motion_sense_set_motion_intervals();

			/* Force a collection to purge old events.  */
			task_set_event(TASK_ID_MOTIONSENSE,
					TASK_EVENT_MOTION_ODR_CHANGE, 0);
		}

		out->ec_rate.ret = motion_sense_ec_rate(sensor) / MSEC;

		args->response_size = sizeof(out->ec_rate);
		break;

	case MOTIONSENSE_CMD_SENSOR_ODR:
		/* Verify sensor number is valid. */
		sensor = host_sensor_id_to_real_sensor(
				in->sensor_odr.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;

		/* Set new data rate if the data arg has a value. */
		if (in->sensor_odr.data != EC_MOTION_SENSE_NO_VALUE) {
			sensor->config[SENSOR_CONFIG_AP].odr =
				in->sensor_odr.data |
				(in->sensor_odr.roundup ? ROUND_UP_FLAG : 0);

			/*
			 * The new ODR may suspend sensor, leaving samples
			 * in the FIFO. Flush it explicitly.
			 */
			deprecated_atomic_or(&odr_event_required,
					     1 << (sensor - motion_sensors));
			task_set_event(TASK_ID_MOTIONSENSE,
					TASK_EVENT_MOTION_ODR_CHANGE, 0);
		}

		out->sensor_odr.ret = sensor->drv->get_data_rate(sensor);

		args->response_size = sizeof(out->sensor_odr);

		break;

	case MOTIONSENSE_CMD_SENSOR_RANGE:
		/* Verify sensor number is valid. */
		sensor = host_sensor_id_to_real_sensor(
				in->sensor_range.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;
		/* Set new range if the data arg has a value. */
		if (in->sensor_range.data != EC_MOTION_SENSE_NO_VALUE) {
			if (!sensor->drv->set_range)
				return EC_RES_INVALID_COMMAND;

			if (sensor->drv->set_range(sensor,
						   in->sensor_range.data,
						   in->sensor_range.roundup)
					!= EC_SUCCESS) {
				return EC_RES_INVALID_PARAM;
			}
		}

		if (!sensor->drv->get_range)
			return EC_RES_INVALID_COMMAND;

		out->sensor_range.ret = sensor->drv->get_range(sensor);
		args->response_size = sizeof(out->sensor_range);
		break;

	case MOTIONSENSE_CMD_SENSOR_OFFSET:
		/* Verify sensor number is valid. */
		sensor = host_sensor_id_to_real_sensor(
				in->sensor_offset.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;
		/* Set new range if the data arg has a value. */
		if (in->sensor_offset.flags & MOTION_SENSE_SET_OFFSET) {
			if (!sensor->drv->set_offset)
				return EC_RES_INVALID_COMMAND;

			ret = sensor->drv->set_offset(sensor,
						in->sensor_offset.offset,
						in->sensor_offset.temp);
			if (ret != EC_SUCCESS)
				return ret;
		}

		if (!sensor->drv->get_offset)
			return EC_RES_INVALID_COMMAND;

		ret = sensor->drv->get_offset(sensor, out->sensor_offset.offset,
				&out->sensor_offset.temp);
		if (ret != EC_SUCCESS)
			return ret;
		args->response_size = sizeof(out->sensor_offset);
		break;

	case MOTIONSENSE_CMD_SENSOR_SCALE:
		/* Verify sensor number is valid. */
		sensor = host_sensor_id_to_real_sensor(
				in->sensor_scale.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;
		/* Set new range if the data arg has a value. */
		if (in->sensor_scale.flags & MOTION_SENSE_SET_OFFSET) {
			if (!sensor->drv->set_scale)
				return EC_RES_INVALID_COMMAND;

			ret = sensor->drv->set_scale(sensor,
						in->sensor_scale.scale,
						in->sensor_scale.temp);
			if (ret != EC_SUCCESS)
				return ret;
		}

		if (!sensor->drv->get_scale)
			return EC_RES_INVALID_COMMAND;

		ret = sensor->drv->get_scale(sensor, out->sensor_scale.scale,
				&out->sensor_scale.temp);
		if (ret != EC_SUCCESS)
			return ret;
		args->response_size = sizeof(out->sensor_scale);
		break;

	case MOTIONSENSE_CMD_PERFORM_CALIB:
		/* Verify sensor number is valid. */
		sensor = host_sensor_id_to_real_sensor(
				in->perform_calib.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;
		if (!sensor->drv->perform_calib)
			return EC_RES_INVALID_COMMAND;

		ret = sensor->drv->perform_calib(
				sensor, in->perform_calib.enable);
		if (ret != EC_SUCCESS)
			return ret;
		ret = sensor->drv->get_offset(sensor, out->perform_calib.offset,
				&out->perform_calib.temp);
		if (ret != EC_SUCCESS)
			return ret;
		args->response_size = sizeof(out->perform_calib);
		break;

	case MOTIONSENSE_CMD_FIFO_FLUSH:
		if (!IS_ENABLED(CONFIG_ACCEL_FIFO))
			return EC_RES_INVALID_PARAM;
		sensor = host_sensor_id_to_real_sensor(
				in->sensor_odr.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;

		deprecated_atomic_add(&sensor->flush_pending, 1);

		task_set_event(TASK_ID_MOTIONSENSE,
			       TASK_EVENT_MOTION_FLUSH_PENDING, 0);
		/* pass-through */
	case MOTIONSENSE_CMD_FIFO_INFO:
		if (!IS_ENABLED(CONFIG_ACCEL_FIFO)) {
			/*
			 * Only support the INFO command, to tell there is no
			 * FIFO.
			 */
			memset(&out->fifo_info, 0, sizeof(out->fifo_info));
			args->response_size = sizeof(out->fifo_info);
			break;
		}
		motion_sense_fifo_get_info(&out->fifo_info, 1);
		for (i = 0; i < motion_sensor_count; i++) {
			out->fifo_info.lost[i] = motion_sensors[i].lost;
			motion_sensors[i].lost = 0;
		}
		args->response_size = sizeof(out->fifo_info) +
			sizeof(uint16_t) * motion_sensor_count;
		break;

	case MOTIONSENSE_CMD_FIFO_READ:
		if (!IS_ENABLED(CONFIG_ACCEL_FIFO))
			return EC_RES_INVALID_PARAM;
		out->fifo_read.number_data = motion_sense_fifo_read(
			args->response_max - sizeof(out->fifo_read),
			in->fifo_read.max_data_vector,
			out->fifo_read.data,
			&(args->response_size));
		args->response_size += sizeof(out->fifo_read);
		break;
	case MOTIONSENSE_CMD_FIFO_INT_ENABLE:
		if (!IS_ENABLED(CONFIG_ACCEL_FIFO))
			return EC_RES_INVALID_PARAM;
		switch (in->fifo_int_enable.enable) {
		case 0:
		case 1:
			fifo_int_enabled = in->fifo_int_enable.enable;
			/* fallthrough */
		case EC_MOTION_SENSE_NO_VALUE:
			out->fifo_int_enable.ret = fifo_int_enabled;
			args->response_size = sizeof(out->fifo_int_enable);
			break;
		default:
			return EC_RES_INVALID_PARAM;
		}
		break;
	case MOTIONSENSE_CMD_ONLINE_CALIB_READ:
		if (!IS_ENABLED(CONFIG_ONLINE_CALIB))
			return EC_RES_INVALID_PARAM;
		sensor = host_sensor_id_to_real_sensor(
			in->online_calib_read.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;


		args->response_size =
			online_calibration_read(
				(int)(sensor - motion_sensors),
				out->online_calib_read.data)
			? sizeof(struct ec_response_online_calibration_data)
			: 0;
		break;
#ifdef CONFIG_GESTURE_HOST_DETECTION
	case MOTIONSENSE_CMD_LIST_ACTIVITIES: {
		uint32_t enabled, disabled, mask, i;

		out->list_activities.enabled = 0;
		out->list_activities.disabled = 0;
		ret = EC_RES_SUCCESS;
		mask = CONFIG_GESTURE_DETECTION_MASK;
		while (mask && ret == EC_RES_SUCCESS) {
			i = get_next_bit(&mask);
			sensor = &motion_sensors[i];
			ret = sensor->drv->list_activities(sensor,
					&enabled, &disabled);
			if (ret == EC_RES_SUCCESS) {
				out->list_activities.enabled |= enabled;
				out->list_activities.disabled |= disabled;
			}
		}
#ifdef CONFIG_BODY_DETECTION
		if (body_detect_get_enable()) {
			out->list_activities.enabled |=
				BIT(MOTIONSENSE_ACTIVITY_BODY_DETECTION);
		} else {
			out->list_activities.disabled |=
				BIT(MOTIONSENSE_ACTIVITY_BODY_DETECTION);
		}
#endif
		if (ret != EC_RES_SUCCESS)
			return ret;
		args->response_size = sizeof(out->list_activities);
		break;
	}
	case MOTIONSENSE_CMD_SET_ACTIVITY: {
		uint32_t enabled, disabled, mask, i;

		mask = CONFIG_GESTURE_DETECTION_MASK;
		ret = EC_RES_SUCCESS;
		while (mask && ret == EC_RES_SUCCESS) {
			i = get_next_bit(&mask);
			sensor = &motion_sensors[i];
			sensor->drv->list_activities(sensor,
					&enabled, &disabled);
			if ((1 << in->set_activity.activity) &
			    (enabled | disabled))
				ret = sensor->drv->manage_activity(sensor,
						in->set_activity.activity,
						in->set_activity.enable,
						&in->set_activity);
		}
#ifdef CONFIG_BODY_DETECTION
		if (in->set_activity.activity ==
		    MOTIONSENSE_ACTIVITY_BODY_DETECTION)
			body_detect_set_enable(in->set_activity.enable);
#endif
		if (ret != EC_RES_SUCCESS)
			return ret;
		args->response_size = 0;
		break;
	}
	case MOTIONSENSE_CMD_GET_ACTIVITY: {
		switch (in->get_activity.activity) {
#ifdef CONFIG_BODY_DETECTION
		case MOTIONSENSE_ACTIVITY_BODY_DETECTION:
			out->get_activity.state = (uint8_t)
					body_detect_get_state();
			ret = EC_RES_SUCCESS;
			break;
#endif
		default:
			ret = EC_RES_INVALID_PARAM;
		}
		if (ret != EC_RES_SUCCESS)
			return ret;
		args->response_size = sizeof(out->get_activity);
		break;
	}
#endif /* defined(CONFIG_GESTURE_HOST_DETECTION) */

#ifdef CONFIG_ACCEL_SPOOF_MODE
	case MOTIONSENSE_CMD_SPOOF: {
		sensor = host_sensor_id_to_real_sensor(in->spoof.sensor_id);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;

		switch (in->spoof.spoof_enable) {
		case MOTIONSENSE_SPOOF_MODE_DISABLE:
			/* Disable spoof mode. */
			sensor->flags &= ~MOTIONSENSE_FLAG_IN_SPOOF_MODE;
			break;

		case MOTIONSENSE_SPOOF_MODE_CUSTOM:
			/*
			 * Enable spoofing, but use provided component values.
			 */
			sensor->spoof_xyz[X] = (int)in->spoof.components[X];
			sensor->spoof_xyz[Y] = (int)in->spoof.components[Y];
			sensor->spoof_xyz[Z] = (int)in->spoof.components[Z];
			sensor->flags |= MOTIONSENSE_FLAG_IN_SPOOF_MODE;
			break;

		case MOTIONSENSE_SPOOF_MODE_LOCK_CURRENT:
			/*
			 * Enable spoofing, but lock to current sensor
			 * values.  raw_xyz already has the values we want.
			 */
			sensor->spoof_xyz[X] = sensor->raw_xyz[X];
			sensor->spoof_xyz[Y] = sensor->raw_xyz[Y];
			sensor->spoof_xyz[Z] = sensor->raw_xyz[Z];
			sensor->flags |= MOTIONSENSE_FLAG_IN_SPOOF_MODE;
			break;

		case MOTIONSENSE_SPOOF_MODE_QUERY:
			/* Querying the spoof status of the sensor. */
			out->spoof.ret = !!(sensor->flags &
					MOTIONSENSE_FLAG_IN_SPOOF_MODE);
			args->response_size = sizeof(out->spoof);
			break;

		default:
			return EC_RES_INVALID_PARAM;
		}

		/*
		 * Only print the status when spoofing is enabled or disabled.
		 */
		if (in->spoof.spoof_enable != MOTIONSENSE_SPOOF_MODE_QUERY)
			print_spoof_mode_status((int)(sensor - motion_sensors));

		break;
	}
#endif /* defined(CONFIG_ACCEL_SPOOF_MODE) */

	default:
		/* Call other users of the motion task */
#ifdef CONFIG_LID_ANGLE
		if (ret == EC_RES_INVALID_PARAM)
			ret = host_cmd_motion_lid(args);
#endif
		return ret;
	}

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_MOTION_SENSE_CMD, host_cmd_motion_sense,
		     EC_VER_MASK(1) | EC_VER_MASK(2) | EC_VER_MASK(3) |
		     EC_VER_MASK(4));

/*****************************************************************************/
/* Console commands */
#ifdef CONFIG_CMD_ACCELS
static int command_accelrange(int argc, char **argv)
{
	char *e;
	int id, data, round = 1;
	struct motion_sensor_t *sensor;

	if (argc < 2 || argc > 4)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is sensor id. */
	id = strtoi(argv[1], &e, 0);
	if (*e || id < 0 || id >= motion_sensor_count)
		return EC_ERROR_PARAM1;

	sensor = &motion_sensors[id];

	if (argc >= 3) {
		/* Second argument is data to write. */
		data = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		if (argc == 4) {
			/* Third argument is rounding flag. */
			round = strtoi(argv[3], &e, 0);
			if (*e)
				return EC_ERROR_PARAM3;
		}

		/*
		 * Write new range, if it returns invalid arg, then return
		 * a parameter error.
		 */
		if (sensor->drv->set_range(sensor,
					   data,
					   round) == EC_ERROR_INVAL)
			return EC_ERROR_PARAM2;
	} else {
		ccprintf("Range for sensor %d: %d\n", id,
			 sensor->drv->get_range(sensor));
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelrange, command_accelrange,
	"id [data [roundup]]",
	"Read or write accelerometer range");

static int command_accelresolution(int argc, char **argv)
{
	char *e;
	int id, data, round = 1;
	struct motion_sensor_t *sensor;

	if (argc < 2 || argc > 4)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is sensor id. */
	id = strtoi(argv[1], &e, 0);
	if (*e || id < 0 || id >= motion_sensor_count)
		return EC_ERROR_PARAM1;

	sensor = &motion_sensors[id];

	if (argc >= 3) {
		/* Second argument is data to write. */
		data = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		if (argc == 4) {
			/* Third argument is rounding flag. */
			round = strtoi(argv[3], &e, 0);
			if (*e)
				return EC_ERROR_PARAM3;
		}

		/*
		 * Write new resolution, if it returns invalid arg, then
		 * return a parameter error.
		 */
		if (sensor->drv->set_resolution &&
		    sensor->drv->set_resolution(sensor, data, round)
			== EC_ERROR_INVAL)
			return EC_ERROR_PARAM2;
	} else {
		ccprintf("Resolution for sensor %d: %d\n", id,
			 sensor->drv->get_resolution(sensor));
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelres, command_accelresolution,
	"id [data [roundup]]",
	"Read or write accelerometer resolution");

static int command_accel_data_rate(int argc, char **argv)
{
	char *e;
	int id, data, round = 1;
	struct motion_sensor_t *sensor;
	enum sensor_config config_id;

	if (argc < 2 || argc > 4)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is sensor id. */
	id = strtoi(argv[1], &e, 0);
	if (*e || id < 0 || id >= motion_sensor_count)
		return EC_ERROR_PARAM1;

	sensor = &motion_sensors[id];

	if (argc >= 3) {
		/* Second argument is data to write. */
		data = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		if (argc == 4) {
			/* Third argument is rounding flag. */
			round = strtoi(argv[3], &e, 0);
			if (*e)
				return EC_ERROR_PARAM3;
		}

		/*
		 * Take ownership of the sensor and
		 * Write new data rate, if it returns invalid arg, then
		 * return a parameter error.
		 */
		config_id = motion_sense_get_ec_config();
		sensor->config[SENSOR_CONFIG_AP].odr = 0;
		sensor->config[config_id].odr =
			data | (round ? ROUND_UP_FLAG : 0);

		deprecated_atomic_or(&odr_event_required,
				     1 << (sensor - motion_sensors));
		task_set_event(TASK_ID_MOTIONSENSE,
				TASK_EVENT_MOTION_ODR_CHANGE, 0);
	} else {
		ccprintf("Data rate for sensor %d: %d\n", id,
			 sensor->drv->get_data_rate(sensor));
		ccprintf("EC rate for sensor %d: %d\n", id,
			 motion_sense_ec_rate(sensor));
		ccprintf("Current Interrupt rate: %d\n", ap_event_interval);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelrate, command_accel_data_rate,
	"id [data [roundup]]",
	"Read or write accelerometer ODR");

static int command_accel_read_xyz(int argc, char **argv)
{
	char *e;
	int id, n = 1, ret;
	struct motion_sensor_t *sensor;
	intv3_t v;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is sensor id. */
	id = strtoi(argv[1], &e, 0);

	if (*e || id < 0 || id >= motion_sensor_count)
		return EC_ERROR_PARAM1;

	if (argc >= 3)
		n = strtoi(argv[2], &e, 0);

	sensor = &motion_sensors[id];

	while ((n == -1) || (n-- > 0)) {
		ret = sensor->drv->read(sensor, v);
		if (ret == 0)
			ccprintf("Current data %d: %-5d %-5d %-5d\n",
				 id, v[X], v[Y], v[Z]);
		else
			ccprintf("vector not ready\n");
		ccprintf("Last calib. data %d: %-5d %-5d %-5d\n",
			 id, sensor->xyz[X], sensor->xyz[Y], sensor->xyz[Z]);
		task_wait_event(motion_min_interval);
	}
	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(accelread, command_accel_read_xyz,
	"id [n]",
	"Read sensor x/y/z");

static int command_accel_init(int argc, char **argv)
{
	char *e;
	int id, ret;
	struct motion_sensor_t *sensor;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is sensor id. */
	id = strtoi(argv[1], &e, 0);

	if (*e || id < 0 || id >= motion_sensor_count)
		return EC_ERROR_PARAM1;

	sensor = &motion_sensors[id];
	ret = motion_sense_init(sensor);

	ccprintf("%s: state %d - %d\n", sensor->name, sensor->state, ret);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelinit, command_accel_init,
	"id",
	"Init sensor");

#ifdef CONFIG_CMD_ACCEL_INFO
static int command_display_accel_info(int argc, char **argv)
{
	int val, i, j;

	if (argc > 3)
		return EC_ERROR_PARAM_COUNT;

	ccprintf("Motion sensors count = %d\n", motion_sensor_count);

	/* Print motion sensor info. */
	for (i = 0; i < motion_sensor_count; i++) {
		ccprintf("\nsensor %d name: %s\n", i, motion_sensors[i].name);
		ccprintf("active mask: %d\n", motion_sensors[i].active_mask);
		ccprintf("chip: %d\n", motion_sensors[i].chip);
		ccprintf("type: %d\n", motion_sensors[i].type);
		ccprintf("location: %d\n", motion_sensors[i].location);
		ccprintf("port: %d\n", motion_sensors[i].port);
		ccprintf("addr: %d\n", I2C_GET_ADDR(motion_sensors[i]
						    .i2c_spi_addr_flags));
		ccprintf("range: %d\n", motion_sensors[i].default_range);
		ccprintf("min_freq: %d\n", motion_sensors[i].min_frequency);
		ccprintf("max_freq: %d\n", motion_sensors[i].max_frequency);
		ccprintf("config:\n");
		for (j = 0; j < SENSOR_CONFIG_MAX; j++) {
			ccprintf("%d - odr: %umHz, ec_rate: %uus\n", j,
				motion_sensors[i].config[j].odr &
				~ROUND_UP_FLAG,
				motion_sensors[i].config[j].ec_rate);
		}
	}

	/* First argument is on/off whether to display accel data. */
	if (argc > 1) {
		if (!parse_bool(argv[1], &val))
			return EC_ERROR_PARAM1;

		accel_disp = val;
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelinfo, command_display_accel_info,
	"on/off [interval]",
	"Print motion sensor info, lid angle calculations"
	" and set calculation frequency.");
#endif /* CONFIG_CMD_ACCEL_INFO */

#ifdef CONFIG_CMD_ACCEL_FIFO
static int motion_sense_read_fifo(int argc, char **argv)
{
	int count, i;
	struct ec_response_motion_sensor_data v;

	if (argc < 1)
		return EC_ERROR_PARAM_COUNT;

	/* Limit the amount of data to avoid saturating the UART buffer */
	count = MIN(queue_count(&motion_sense_fifo), 16);
	for (i = 0; i < count; i++) {
		queue_peek_units(&motion_sense_fifo, &v, i, 1);
		if (v.flags & (MOTIONSENSE_SENSOR_FLAG_TIMESTAMP |
			       MOTIONSENSE_SENSOR_FLAG_FLUSH)) {
			uint64_t timestamp;
			memcpy(&timestamp, v.data, sizeof(v.data));
			ccprintf("Timestamp: 0x%016llx%s\n", timestamp,
				 (v.flags & MOTIONSENSE_SENSOR_FLAG_FLUSH ?
				  " - Flush" : ""));
		} else {
			ccprintf("%d %d: %-5d %-5d %-5d\n", i, v.sensor_num,
				 v.data[X], v.data[Y], v.data[Z]);
		}
	}
	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(fiforead, motion_sense_read_fifo,
	"id",
	"Read Fifo sensor");
#endif /* defined(CONFIG_CMD_ACCEL_FIFO) */
#endif /* CONFIG_CMD_ACCELS */

#ifdef CONFIG_ACCEL_SPOOF_MODE
static void print_spoof_mode_status(int id)
{
	CPRINTS("Sensor %d spoof mode is %s. <%d, %d, %d>", id,
		(motion_sensors[id].flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE)
				? "enabled" : "disabled",
		motion_sensors[id].spoof_xyz[X],
		motion_sensors[id].spoof_xyz[Y],
		motion_sensors[id].spoof_xyz[Z]);
}

#ifdef CONFIG_CMD_ACCELSPOOF
static int command_accelspoof(int argc, char **argv)
{
	char *e;
	int id, enable, i;
	struct motion_sensor_t *s;

	/* There must be at least 1 parameter, the sensor id. */
	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is sensor id. */
	id = strtoi(argv[1], &e, 0);
	if (id >= motion_sensor_count || id < 0)
		return EC_ERROR_PARAM1;

	s = &motion_sensors[id];

	/* Print the sensor's current spoof status. */
	if (argc == 2)
		print_spoof_mode_status(id);

	/* Enable/Disable spoof mode. */
	if (argc >= 3) {
		if (!parse_bool(argv[2], &enable))
			return EC_ERROR_PARAM2;

		if (enable) {
			/*
			 * If no components are provided, we'll just use the
			 * current values as the spoofed values.  But if the
			 * components are provided, use the provided ones as the
			 * spoofed ones.
			 */
			if (argc == 6) {
				for (i = 0; i < 3; i++)
					s->spoof_xyz[i] = strtoi(argv[3 + i],
								 &e, 0);
			} else if (argc == 3) {
				for (i = X; i <= Z; i++)
					s->spoof_xyz[i] = s->raw_xyz[i];
			} else {
				/* It's either all or nothing. */
				return EC_ERROR_PARAM_COUNT;
			}
		}
		if (enable)
			s->flags |= MOTIONSENSE_FLAG_IN_SPOOF_MODE;
		else
			s->flags &= ~MOTIONSENSE_FLAG_IN_SPOOF_MODE;
		print_spoof_mode_status(id);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelspoof, command_accelspoof,
			"id [on/off] [X] [Y] [Z]",
			"Enable/Disable spoofing of sensor readings.");
#endif /* defined(CONFIG_CMD_ACCELSPOOF) */
#endif /* defined(CONFIG_ACCEL_SPOOF_MODE) */
