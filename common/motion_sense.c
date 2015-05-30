/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Motion sense module to read from various motion sensors. */

#include "accelgyro.h"
#include "atomic.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gesture.h"
#include "hooks.h"
#include "host_command.h"
#include "hwtimer.h"
#include "lid_angle.h"
#include "math_util.h"
#include "mkbp_event.h"
#include "motion_sense.h"
#include "motion_lid.h"
#include "power.h"
#include "queue.h"
#include "timer.h"
#include "task.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_MOTION_SENSE, outstr)
#define CPRINTS(format, args...) cprints(CC_MOTION_SENSE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_MOTION_SENSE, format, ## args)

/* Minimum time in between running motion sense task loop. */
#define MIN_MOTION_SENSE_WAIT_TIME (1 * MSEC)

/* Time to wait in between failed attempts to initialize sensors */
#define TASK_MOTION_SENSE_WAIT_TIME (500 * MSEC)


/* Bounds for setting the sensor polling interval. */
#define MIN_POLLING_INTERVAL_MS 1

/* Define sensor sampling interval in suspend. */
#ifdef CONFIG_GESTURE_DETECTION
#define SUSPEND_SAMPLING_INTERVAL (CONFIG_GESTURE_SAMPLING_INTERVAL_MS * MSEC)
#elif defined(CONFIG_ACCEL_FIFO)
#define SUSPEND_SAMPLING_INTERVAL (1000 * MSEC)
#else
#define SUSPEND_SAMPLING_INTERVAL (100 * MSEC)
#endif

/* Accelerometer polling intervals based on chipset state. */
#ifdef CONFIG_ACCEL_FIFO
/*
 * TODO(crbug.com/498352):
 * For now, we collect the data every accel_interval.
 * For non-FIFO sensor, we should set accel_interval at a lower value.
 * But if we have a mix a FIFO and non FIFO sensors, setting it
 * to low will increase the number of interrupts.
 */
static int accel_interval_ap_on = 1000 * MSEC;
#else
static int accel_interval_ap_on = 10 * MSEC;
#endif
/*
 * Sampling interval for measuring acceleration and calculating lid angle.
 * Set to accel_interval_ap_on when ap is on.
 */
static int accel_interval;

#ifdef CONFIG_CMD_ACCEL_INFO
static int accel_disp;
#endif

/*
 * Mutex to protect sensor values between host command task and
 * motion sense task:
 * When we process CMD_DUMP, we want to be sure the motion sense
 * task is not updating the sensor values at the same time.
 */
static struct mutex g_sensor_mutex;

#ifdef CONFIG_ACCEL_FIFO
struct queue motion_sense_fifo = QUEUE_NULL(CONFIG_ACCEL_FIFO,
		struct ec_response_motion_sensor_data);
int motion_sense_fifo_lost;

static void *nullcpy(void *dest, const void *src, size_t n)
{
	return dest;
}

void motion_sense_fifo_add_unit(struct ec_response_motion_sensor_data *data,
				const struct motion_sensor_t *sensor)
{
	data->sensor_num = (sensor - motion_sensors);
	mutex_lock(&g_sensor_mutex);
	if (queue_space(&motion_sense_fifo) == 0) {
		motion_sense_fifo_lost++;
		queue_remove_memcpy(&motion_sense_fifo, NULL, 1,
				nullcpy);
	}
	mutex_unlock(&g_sensor_mutex);
	queue_add_unit(&motion_sense_fifo, data);
}

static inline void motion_sense_insert_flush(
		const struct motion_sensor_t *sensor)
{
	struct ec_response_motion_sensor_data vector;
	vector.flags = MOTIONSENSE_SENSOR_FLAG_FLUSH |
		       MOTIONSENSE_SENSOR_FLAG_TIMESTAMP;
	vector.timestamp = __hw_clock_source_read();
	motion_sense_fifo_add_unit(&vector, sensor);
}

static inline void motion_sense_insert_timestamp(void)
{
	struct ec_response_motion_sensor_data vector;
	vector.flags = MOTIONSENSE_SENSOR_FLAG_TIMESTAMP;
	vector.timestamp = __hw_clock_source_read();
	motion_sense_fifo_add_unit(&vector, motion_sensors);
}

static void motion_sense_get_fifo_info(
		struct ec_response_motion_sense_fifo_info *fifo_info)
{
	fifo_info->size = motion_sense_fifo.buffer_units;
	mutex_lock(&g_sensor_mutex);
	fifo_info->count = queue_count(&motion_sense_fifo);
	fifo_info->lost = motion_sense_fifo_lost;
	motion_sense_fifo_lost = 0;
	mutex_unlock(&g_sensor_mutex);
	fifo_info->timestamp = __hw_clock_source_read();
}
#endif

static void motion_sense_shutdown(void)
{
	int i;
	struct motion_sensor_t *sensor;

	for (i = 0; i < motion_sensor_count; i++) {
		sensor = &motion_sensors[i];
		sensor->active = SENSOR_ACTIVE_S5;
		sensor->runtime_config.odr    = sensor->default_config.odr;
		sensor->runtime_config.range  = sensor->default_config.range;
		if ((sensor->state == SENSOR_INITIALIZED) &&
		   !(sensor->active_mask & sensor->active)) {
			sensor->drv->set_data_rate(sensor, 0, 0);
			sensor->state = SENSOR_NOT_INITIALIZED;
		}
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, motion_sense_shutdown,
	     MOTION_SENSE_HOOK_PRIO);

static void motion_sense_suspend(void)
{
	int i;
	struct motion_sensor_t *sensor;

	accel_interval = SUSPEND_SAMPLING_INTERVAL;

	for (i = 0; i < motion_sensor_count; i++) {
		sensor = &motion_sensors[i];

		/* if it is in s5, don't enter suspend */
		if (sensor->active == SENSOR_ACTIVE_S5)
			continue;

		sensor->active = SENSOR_ACTIVE_S3;

		/* Saving power if the sensor is not active in S3 */
		if ((sensor->state == SENSOR_INITIALIZED) &&
		    !(sensor->active_mask & sensor->active)) {
			sensor->drv->set_data_rate(sensor, 0, 0);
			sensor->state = SENSOR_NOT_INITIALIZED;
		}
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, motion_sense_suspend,
	     MOTION_SENSE_HOOK_PRIO);

static void motion_sense_resume(void)
{
	int i;
	struct motion_sensor_t *sensor;

	accel_interval = accel_interval_ap_on;

	for (i = 0; i < motion_sensor_count; i++) {
		sensor = &motion_sensors[i];
		sensor->active = SENSOR_ACTIVE_S0;
		if (sensor->state == SENSOR_INITIALIZED) {
			/* Put back the odr previously set. */
			sensor->drv->set_data_rate(sensor,
					sensor->runtime_config.odr, 1);
		}
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, motion_sense_resume,
	     MOTION_SENSE_HOOK_PRIO);

/* Write to LPC status byte to represent that accelerometers are present. */
static inline void set_present(uint8_t *lpc_status)
{
	*lpc_status |= EC_MEMMAP_ACC_STATUS_PRESENCE_BIT;
}

/* Update/Write LPC data */
static inline void update_sense_data(uint8_t *lpc_status,
		uint16_t *lpc_data, int *psample_id)
{
	int i;
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
	 * with un-calibrated accels. The AP calculates a separate,
	 * more accurate lid angle.
	 */
#ifdef CONFIG_LID_ANGLE
	lpc_data[0] = motion_lid_get_angle();
#else
	lpc_data[0] = LID_ANGLE_UNRELIABLE;
#endif
	for (i = 0; i < motion_sensor_count; i++) {
		sensor = &motion_sensors[i];
		lpc_data[1+3*i] = sensor->xyz[X];
		lpc_data[2+3*i] = sensor->xyz[Y];
		lpc_data[3+3*i] = sensor->xyz[Z];
	}

	/*
	 * Increment sample id and clear busy bit to signal we finished
	 * updating data.
	 */
	*psample_id = (*psample_id + 1) &
			EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK;
	*lpc_status = EC_MEMMAP_ACC_STATUS_PRESENCE_BIT | *psample_id;
}

static inline void motion_sense_init(struct motion_sensor_t *sensor)
{
	int ret, cnt = 3;

	/* Initialize accelerometers. */
	do {
		ret = sensor->drv->init(sensor);
	} while ((ret != EC_SUCCESS) && (--cnt > 0));

	if (ret != EC_SUCCESS)
		sensor->state = SENSOR_INIT_ERROR;
	else
		sensor->state = SENSOR_INITIALIZED;
}

static int motion_sense_read(struct motion_sensor_t *sensor)
{
	if (sensor->state != SENSOR_INITIALIZED)
		return EC_ERROR_UNKNOWN;

	/* Read all raw X,Y,Z accelerations. */
	return sensor->drv->read(sensor, sensor->raw_xyz);
}

static int motion_sense_process(struct motion_sensor_t *sensor,
				uint32_t event,
				int *flush_needed)
{
	int ret = EC_SUCCESS;

#ifdef CONFIG_ACCEL_INTERRUPTS
	if ((event & TASK_EVENT_MOTION_INTERRUPT) &&
	    (sensor->drv->irq_handler != NULL))
		sensor->drv->irq_handler(sensor);
#endif
#ifdef CONFIG_ACCEL_FIFO
	if (sensor->drv->load_fifo != NULL) {
		/* Load fifo is filling raw_xyz sensor vector */
		sensor->drv->load_fifo(sensor);
	} else {
		ret = motion_sense_read(sensor);
		/* Put data in fifo.
		 * Depending on the frequency on that particular sensor,
		 * we may not do it all the time.
		 * TODO(chromium:498352)
		 */
	}
	if (event & TASK_EVENT_MOTION_FLUSH_PENDING) {
		int flush_pending;
		flush_pending = atomic_read_clear(&sensor->flush_pending);
		for (; flush_pending > 0; flush_pending--) {
			*flush_needed = 1;
			motion_sense_insert_flush(sensor);
		}
	}
#else
	/* Get latest data for local calculation */
	ret = motion_sense_read(sensor);
#endif
	return ret;
}

/*
 * Motion Sense Task
 * Requirement: motion_sensors[] are defined in board.c file.
 * Two (minimium) Accelerometers:
 *    1 in the A/B(lid, display) and 1 in the C/D(base, keyboard)
 * Gyro Sensor (optional)
 */
void motion_sense_task(void)
{
	int i, ret, wait_us, fifo_flush_needed = 0;
	static timestamp_t ts_begin_task, ts_end_task;
	uint8_t *lpc_status;
	uint16_t *lpc_data;
	uint32_t event;
	int sample_id = 0;
	int rd_cnt;
	struct motion_sensor_t *sensor;
#ifdef CONFIG_ACCEL_FIFO
	static timestamp_t ts_last_int;
#endif

	lpc_status = host_get_memmap(EC_MEMMAP_ACC_STATUS);
	lpc_data = (uint16_t *)host_get_memmap(EC_MEMMAP_ACC_DATA);

	for (i = 0; i < motion_sensor_count; ++i) {
		sensor = &motion_sensors[i];
		sensor->state = SENSOR_NOT_INITIALIZED;

		sensor->runtime_config.odr = sensor->default_config.odr;
		sensor->runtime_config.range = sensor->default_config.range;
	}

	set_present(lpc_status);

	if (chipset_in_state(CHIPSET_STATE_ON)) {
		/* Update the sensor current active state to S0. */
		for (i = 0; i < motion_sensor_count; ++i) {
			sensor = &motion_sensors[i];
			sensor->active = SENSOR_ACTIVE_S0;
		}

		accel_interval = accel_interval_ap_on;
	} else {
		/* sensor->active already initializes to SENSOR_ACTIVE_S5 */
		accel_interval = SUSPEND_SAMPLING_INTERVAL;
	}

	wait_us = accel_interval;
#ifdef CONFIG_ACCEL_FIFO
	ts_last_int = get_time();
#endif
	while ((event = task_wait_event(wait_us))) {
		ts_begin_task = get_time();
		rd_cnt = 0;
		for (i = 0; i < motion_sensor_count; ++i) {

			sensor = &motion_sensors[i];

			/* if the sensor is active in the current power state */
			if (sensor->active & sensor->active_mask) {
				if (sensor->state == SENSOR_NOT_INITIALIZED)
					motion_sense_init(sensor);

				ret = motion_sense_process(sensor, event,
						&fifo_flush_needed);
				if (ret != EC_SUCCESS)
					continue;
				rd_cnt++;
				/*
				 * Rotate the accel vector so the reference for
				 * all sensors are in the same space.
				 */
				mutex_lock(&g_sensor_mutex);
				if (*sensor->rot_standard_ref != NULL)
					rotate(sensor->raw_xyz,
					       *sensor->rot_standard_ref,
					       sensor->xyz);
				else
					memcpy(sensor->xyz, sensor->raw_xyz,
						sizeof(vector_3_t));
				mutex_unlock(&g_sensor_mutex);
			}
		}

#ifdef CONFIG_GESTURE_DETECTION
		/* Run gesture recognition engine */
		gesture_calc();
#endif
#ifdef CONFIG_LID_ANGLE
		/*
		 * TODO (crosbug.com/p/36132): Checking for ALL sensors on is
		 * overkill.  It should just check for ACCEL in BASE and ACCEL
		 * in LID, since those are the only ones needed for the lid
		 * calculation.
		 */
		if (rd_cnt == motion_sensor_count)
			motion_lid_calc();
#endif
#ifdef CONFIG_CMD_ACCEL_INFO
		if (accel_disp) {
			CPRINTF("[%T ");
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
		update_sense_data(lpc_status, lpc_data, &sample_id);

		ts_end_task = get_time();
#ifdef CONFIG_ACCEL_FIFO
		/*
		 * If ODR of any sensor changed, insert a timestamp to be ease
		 * calculation of each events.
		 */
		if (event & TASK_EVENT_MOTION_ODR_CHANGE)
			motion_sense_insert_timestamp();

		/*
		 * Ask the host to flush the queue if
		 * - a flush event has been queued.
		 * - the queue is almost full,
		 * - we haven't done it for a while.
		 */
		if (fifo_flush_needed ||
		    queue_space(&motion_sense_fifo) < CONFIG_ACCEL_FIFO_THRES ||
		    (ts_end_task.val - ts_last_int.val) > accel_interval) {
			if (!fifo_flush_needed)
				motion_sense_insert_timestamp();
			fifo_flush_needed = 0;
			ts_last_int = ts_end_task;
			mkbp_send_event(EC_MKBP_EVENT_SENSOR_FIFO);
		}
#endif
		/* Delay appropriately to keep sampling time consistent. */
		wait_us = accel_interval -
			(ts_end_task.val - ts_begin_task.val);

		/*
		 * Guarantee some minimum delay to allow other lower priority
		 * tasks to run.
		 */
		if (wait_us < MIN_MOTION_SENSE_WAIT_TIME)
			wait_us = MIN_MOTION_SENSE_WAIT_TIME;

	}
}

#ifdef CONFIG_ACCEL_FIFO
static int motion_sense_get_next_event(uint8_t *out)
{
	union ec_response_get_next_data *data =
		(union ec_response_get_next_data *)out;
	/* out is not padded. It has one byte for the event type */
	motion_sense_get_fifo_info(&data->sensor_fifo.info);
	return sizeof(data->sensor_fifo);
}

DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_SENSOR_FIFO, motion_sense_get_next_event);
#endif
/*****************************************************************************/
/* Host commands */

/* Function to map host sensor IDs to motion sensor. */
static struct motion_sensor_t
	*host_sensor_id_to_motion_sensor(int host_id)
{
	struct motion_sensor_t *sensor;

	if (host_id >= motion_sensor_count)
		return NULL;
	sensor = &motion_sensors[host_id];

	/* if sensor is powered and initialized, return match */
	if ((sensor->active & sensor->active_mask)
		&& (sensor->state == SENSOR_INITIALIZED))
			return sensor;

	/* If no match then the EC currently doesn't support ID received. */
	return NULL;
}

static int host_cmd_motion_sense(struct host_cmd_handler_args *args)
{
	const struct ec_params_motion_sense *in = args->params;
	struct ec_response_motion_sense *out = args->response;
	struct motion_sensor_t *sensor;
	int i, data, ret = EC_RES_INVALID_PARAM, reported;

	switch (in->cmd) {
	case MOTIONSENSE_CMD_DUMP:
		out->dump.module_flags =
			(*(host_get_memmap(EC_MEMMAP_ACC_STATUS)) &
			 EC_MEMMAP_ACC_STATUS_PRESENCE_BIT) ?
			MOTIONSENSE_MODULE_FLAG_ACTIVE : 0;
		out->dump.sensor_count = motion_sensor_count;
		args->response_size = sizeof(out->dump);
		reported = MIN(motion_sensor_count, in->dump.max_sensor_count);
		mutex_lock(&g_sensor_mutex);
		for (i = 0; i < reported; i++) {
			sensor = &motion_sensors[i];
			out->dump.sensor[i].flags =
				MOTIONSENSE_SENSOR_FLAG_PRESENT;
			/* casting from int to s16 */
			out->dump.sensor[i].data[X] = sensor->xyz[X];
			out->dump.sensor[i].data[Y] = sensor->xyz[Y];
			out->dump.sensor[i].data[Z] = sensor->xyz[Z];
		}
		mutex_unlock(&g_sensor_mutex);
		args->response_size += reported *
			sizeof(struct ec_response_motion_sensor_data);
		break;

	case MOTIONSENSE_CMD_DATA:
		sensor = host_sensor_id_to_motion_sensor(
				in->sensor_odr.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;

		out->data.flags = 0;

		mutex_lock(&g_sensor_mutex);
		out->data.data[X] = sensor->xyz[X];
		out->data.data[Y] = sensor->xyz[Y];
		out->data.data[Z] = sensor->xyz[Z];
		mutex_unlock(&g_sensor_mutex);
		args->response_size = sizeof(out->data);
		break;

	case MOTIONSENSE_CMD_INFO:
		sensor = host_sensor_id_to_motion_sensor(
				in->sensor_odr.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;

		out->info.type = sensor->type;
		out->info.location = sensor->location;
		out->info.chip = sensor->chip;

		args->response_size = sizeof(out->info);
		break;

	case MOTIONSENSE_CMD_EC_RATE:
		/*
		 * Set new sensor sampling rate when AP is on, if the data arg
		 * has a value.
		 */
		if (in->ec_rate.data != EC_MOTION_SENSE_NO_VALUE) {
			/* Bound the new sampling rate. */
			data = in->ec_rate.data;
			if (data < MIN_POLLING_INTERVAL_MS)
				data = MIN_POLLING_INTERVAL_MS;

			accel_interval_ap_on = data * MSEC;
			accel_interval = data * MSEC;
		}

		out->ec_rate.ret = accel_interval_ap_on / MSEC;

		args->response_size = sizeof(out->ec_rate);
		break;

	case MOTIONSENSE_CMD_SENSOR_ODR:
		/* Verify sensor number is valid. */
		sensor = host_sensor_id_to_motion_sensor(
				in->sensor_odr.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;

		/* Set new data rate if the data arg has a value. */
		if (in->sensor_odr.data != EC_MOTION_SENSE_NO_VALUE) {
			if (sensor->drv->set_data_rate(sensor,
						in->sensor_odr.data,
						in->sensor_odr.roundup)
					!= EC_SUCCESS) {
				CPRINTS("MS bad sensor rate %d",
						in->sensor_odr.data);
				return EC_RES_INVALID_PARAM;
			}
		}

		/* To be sure timestamps are calculated properly,
		 * Send an event to have a timestamp inserted in the FIFO.
		 */
		task_set_event(TASK_ID_MOTIONSENSE,
			       TASK_EVENT_MOTION_ODR_CHANGE, 0);

		sensor->drv->get_data_rate(sensor, &data);

		/* Save configuration parameter: ODR */
		sensor->runtime_config.odr = data;
		out->sensor_odr.ret = data;

		args->response_size = sizeof(out->sensor_odr);
		break;

	case MOTIONSENSE_CMD_SENSOR_RANGE:
		/* Verify sensor number is valid. */
		sensor = host_sensor_id_to_motion_sensor(
				in->sensor_range.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;

		/* Set new range if the data arg has a value. */
		if (in->sensor_range.data != EC_MOTION_SENSE_NO_VALUE) {
			if (sensor->drv->set_range(sensor,
						in->sensor_range.data,
						in->sensor_range.roundup)
					!= EC_SUCCESS) {
				CPRINTS("MS bad sensor range %d",
						in->sensor_range.data);
				return EC_RES_INVALID_PARAM;
			}
		}

		sensor->drv->get_range(sensor, &data);

		/* Save configuration parameter: range */
		sensor->runtime_config.range = data;

		out->sensor_range.ret = data;
		args->response_size = sizeof(out->sensor_range);
		break;

	case MOTIONSENSE_CMD_SENSOR_OFFSET:
		/* Verify sensor number is valid. */
		sensor = host_sensor_id_to_motion_sensor(
				in->sensor_offset.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;

		/* Set new range if the data arg has a value. */
		if (in->sensor_offset.flags & MOTION_SENSE_SET_OFFSET) {
			if (sensor->drv->set_offset(sensor,
						in->sensor_offset.offset,
						in->sensor_offset.temp)
					!= EC_SUCCESS) {
				CPRINTS("MS bad sensor offsets");
				return EC_RES_INVALID_PARAM;
			}
		}

		sensor->drv->get_offset(sensor, out->sensor_offset.offset,
				&out->sensor_offset.temp);
		args->response_size = sizeof(out->sensor_offset);
		break;

#ifdef CONFIG_ACCEL_FIFO
	case MOTIONSENSE_CMD_FIFO_FLUSH:
		sensor = host_sensor_id_to_motion_sensor(
				in->sensor_odr.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;
		atomic_add(&sensor->flush_pending, 1);

		task_set_event(TASK_ID_MOTIONSENSE,
			       TASK_EVENT_MOTION_FLUSH_PENDING, 0);
		/* passthrough */
	case MOTIONSENSE_CMD_FIFO_INFO:
		motion_sense_get_fifo_info(&out->fifo_info);
		args->response_size = sizeof(out->fifo_info);
		break;

	case MOTIONSENSE_CMD_FIFO_READ:
		mutex_lock(&g_sensor_mutex);
		reported = MIN((args->response_max - sizeof(out->fifo_read)) /
			       motion_sense_fifo.unit_bytes,
			       MIN(queue_count(&motion_sense_fifo),
				   in->fifo_read.max_data_vector));
		reported = queue_remove_units(&motion_sense_fifo,
				out->fifo_read.data, reported);
		mutex_unlock(&g_sensor_mutex);
		out->fifo_read.number_data = reported;
		args->response_size = sizeof(out->fifo_read) + reported *
			motion_sense_fifo.unit_bytes;
		break;
#else
	case MOTIONSENSE_CMD_FIFO_INFO:
		/* Only support the INFO command, to tell there is no FIFO. */
		memset(&out->fifo_info, 0, sizeof(out->fifo_info));
		args->response_size = sizeof(out->fifo_info);
		break;
#endif
	default:
		/* Call other users of the motion task */
#ifdef CONFIG_LID_ANGLE
		if (ret == EC_RES_INVALID_PARAM)
			ret = host_cmd_motion_lid(args);
#endif
		if (ret == EC_RES_INVALID_PARAM)
			CPRINTS("MS bad cmd 0x%x", in->cmd);
		return ret;
	}

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_MOTION_SENSE_CMD,
		     host_cmd_motion_sense,
		     EC_VER_MASK(1) | EC_VER_MASK(2));

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
		sensor->drv->get_range(sensor, &data);
		ccprintf("Range for sensor %d: %d\n", id, data);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelrange, command_accelrange,
	"id [data [roundup]]",
	"Read or write accelerometer range", NULL);

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
		if (sensor->drv->set_resolution(sensor, data, round)
			== EC_ERROR_INVAL)
			return EC_ERROR_PARAM2;
	} else {
		sensor->drv->get_resolution(sensor, &data);
		ccprintf("Resolution for sensor %d: %d\n", id, data);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelres, command_accelresolution,
	"id [data [roundup]]",
	"Read or write accelerometer resolution", NULL);

static int command_accel_data_rate(int argc, char **argv)
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
		 * Write new data rate, if it returns invalid arg, then
		 * return a parameter error.
		 */
		if (sensor->drv->set_data_rate(sensor, data, round)
			== EC_ERROR_INVAL)
			return EC_ERROR_PARAM2;
	} else {
		sensor->drv->get_data_rate(sensor, &data);
		ccprintf("Data rate for sensor %d: %d\n", id, data);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelrate, command_accel_data_rate,
	"id [data [roundup]]",
	"Read or write accelerometer ODR", NULL);

static int command_accel_read_xyz(int argc, char **argv)
{
	char *e;
	int id, n = 1, ret;
	struct motion_sensor_t *sensor;
	vector_3_t v;

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
			ccprintf("Current raw data %d: %-5d %-5d %-5d\n",
				 id, v[X], v[Y], v[Z]);
		else
			ccprintf("vector not ready\n");
		ccprintf("Last calib. data %d: %-5d %-5d %-5d\n",
			 id, sensor->xyz[X], sensor->xyz[Y], sensor->xyz[Z]);
		task_wait_event(MIN_MOTION_SENSE_WAIT_TIME);
	}
	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(accelread, command_accel_read_xyz,
	"id [n]",
	"Read sensor x/y/z", NULL);

static int command_accel_init(int argc, char **argv)
{
	char *e;
	int id;
	struct motion_sensor_t *sensor;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is sensor id. */
	id = strtoi(argv[1], &e, 0);

	if (*e || id < 0 || id >= motion_sensor_count)
		return EC_ERROR_PARAM1;

	sensor = &motion_sensors[id];
	motion_sense_init(sensor);

	ccprintf("%s: %d\n", sensor->name, sensor->state);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelinit, command_accel_init,
	"id",
	"Init sensor", NULL);

#ifdef CONFIG_CMD_ACCEL_INFO
static int command_display_accel_info(int argc, char **argv)
{
	char *e;
	int val;

	if (argc > 3)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is on/off whether to display accel data. */
	if (argc > 1) {
		if (!parse_bool(argv[1], &val))
			return EC_ERROR_PARAM1;

		accel_disp = val;
	}

	/*
	 * Second arg changes the accel task time interval. Note accel
	 * sampling interval will be clobbered when chipset suspends or
	 * resumes.
	 */
	if (argc > 2) {
		val = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		accel_interval = val * MSEC;
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelinfo, command_display_accel_info,
	"on/off [interval]",
	"Print motion sensor info, lid angle calculations"
	" and set calculation frequency.", NULL);
#endif /* CONFIG_CMD_ACCEL_INFO */

#ifdef CONFIG_ACCEL_INTERRUPTS
/* TODO(crosbug.com/p/426659): this code is broken, does not with ST sensors. */
void accel_int_lid(enum gpio_signal signal)
{
	/*
	 * Print statement is here for testing with console accelint command.
	 * Remove print statement when interrupt is used for real.
	 */
	CPRINTS("Accelerometer wake-up interrupt occurred on lid");
}

void accel_int_base(enum gpio_signal signal)
{
	/*
	 * Print statement is here for testing with console accelint command.
	 * Remove print statement when interrupt is used for real.
	 */
	CPRINTS("Accelerometer wake-up interrupt occurred on base");
}

static int command_accelerometer_interrupt(int argc, char **argv)
{
	char *e;
	int id, thresh;
	struct motion_sensor_t *sensor;

	if (argc != 3)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is id. */
	id = strtoi(argv[1], &e, 0);
	if (*e || id < 0 || id >= motion_sensor_count)
		return EC_ERROR_PARAM1;

	sensor = &motion_sensors[id];

	/* Second argument is interrupt threshold. */
	thresh = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	sensor->drv->set_interrupt(sensor, thresh);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelint, command_accelerometer_interrupt,
	"id threshold",
	"Write interrupt threshold", NULL);
#endif /* CONFIG_ACCEL_INTERRUPTS */

#ifdef CONFIG_ACCEL_FIFO
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
			ccprintf("Timestamp: 0x%016lx%s\n", timestamp,
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
	"Read Fifo sensor", NULL);
#endif

#endif /* CONFIG_CMD_ACCELS */
