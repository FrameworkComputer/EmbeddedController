/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Motion sense module to read from various motion sensors. */

#include "accelgyro.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gesture.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_angle.h"
#include "math_util.h"
#include "motion_sense.h"
#include "motion_lid.h"
#include "power.h"
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

/* For vector_3_t, define which coordinates are in which location. */
enum {
	X, Y, Z
};

/* Bounds for setting the sensor polling interval. */
#define MIN_POLLING_INTERVAL_MS 5
#define MAX_POLLING_INTERVAL_MS 1000

/* Define sensor sampling interval in suspend. */
#ifdef CONFIG_GESTURE_DETECTION
#define SUSPEND_SAMPLING_INTERVAL CONFIG_GESTURE_SAMPLING_INTERVAL_MS
#else
#define SUSPEND_SAMPLING_INTERVAL 100
#endif

/* Accelerometer polling intervals based on chipset state. */
static int accel_interval_ap_on_ms = 10;
/*
 * Sampling interval for measuring acceleration and calculating lid angle.
 * Set to accel_interval_ap_on_ms when ap is on.
 */
static int accel_interval_ms;

#ifdef CONFIG_CMD_ACCEL_INFO
static int accel_disp;
#endif

/*
 * Angle threshold for how close the hinge aligns with gravity before
 * considering the lid angle calculation unreliable. For computational
 * efficiency, value is given unit-less, so if you want the threshold to be
 * at 15 degrees, the value would be cos(15 deg) = 0.96593.
 */
#define HINGE_ALIGNED_WITH_GRAVITY_THRESHOLD 0.96593F


static void motion_sense_shutdown(void)
{
	int i;
	struct motion_sensor_t *sensor;

	for (i = 0; i < motion_sensor_count; i++) {
		sensor = &motion_sensors[i];
		sensor->active = SENSOR_ACTIVE_S5;
		sensor->odr    = sensor->default_odr;
		sensor->range  = sensor->default_range;
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

	accel_interval_ms = SUSPEND_SAMPLING_INTERVAL;

	for (i = 0; i < motion_sensor_count; i++) {
		sensor = &motion_sensors[i];
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

	accel_interval_ms = accel_interval_ap_on_ms;

	for (i = 0; i < motion_sensor_count; i++) {
		sensor = &motion_sensors[i];
		sensor->active = SENSOR_ACTIVE_S0;
		if (sensor->state == SENSOR_INITIALIZED) {
			/* Put back the odr previously set. */
			sensor->drv->set_data_rate(sensor, sensor->odr, 1);
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
	int ret;

	if (sensor->state != SENSOR_INITIALIZED)
		return EC_ERROR_UNKNOWN;

	/* Read all raw X,Y,Z accelerations. */
	ret = sensor->drv->read(sensor,
		&sensor->xyz[X],
		&sensor->xyz[Y],
		&sensor->xyz[Z]);

	if (ret != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
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
	int i;
	int wait_us;
	static timestamp_t ts0, ts1;
	uint8_t *lpc_status;
	uint16_t *lpc_data;
	int sample_id = 0;
	int rd_cnt;
	struct motion_sensor_t *sensor;

	lpc_status = host_get_memmap(EC_MEMMAP_ACC_STATUS);
	lpc_data = (uint16_t *)host_get_memmap(EC_MEMMAP_ACC_DATA);

	for (i = 0; i < motion_sensor_count; ++i) {
		sensor = &motion_sensors[i];
		sensor->state = SENSOR_NOT_INITIALIZED;

		sensor->odr = sensor->default_odr;
		sensor->range = sensor->default_range;
	}

	set_present(lpc_status);

	/* Initialize sampling interval. */
	accel_interval_ms = chipset_in_state(CHIPSET_STATE_ON) ?
			accel_interval_ap_on_ms : SUSPEND_SAMPLING_INTERVAL;

	while (1) {
		ts0 = get_time();
		rd_cnt = 0;
		for (i = 0; i < motion_sensor_count; ++i) {

			sensor = &motion_sensors[i];

			/* if the sensor is active in the current power state */
			if (sensor->active & sensor->active_mask) {

				if (sensor->state == SENSOR_NOT_INITIALIZED)
					motion_sense_init(sensor);

				if (EC_SUCCESS != motion_sense_read(sensor))
					continue;

				rd_cnt++;
				/*
				 * Rotate the accel vector so the reference for
				 * all sensors are in the same space.
				 */
				if (*sensor->rot_standard_ref != NULL) {
					rotate(sensor->xyz,
					       *sensor->rot_standard_ref,
					       sensor->xyz);
				}
			}
		}

#ifdef CONFIG_GESTURE_DETECTION
		/* Run gesture recognition engine */
		gesture_calc();
#endif
#ifdef CONFIG_LID_ANGLE
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
			CPRINTF("a=%-6.1d", 10 * motion_lid_get_angle());
#endif
			CPRINTF("]\n");
		}
#endif
		update_sense_data(lpc_status, lpc_data, &sample_id);

		/* Delay appropriately to keep sampling time consistent. */
		ts1 = get_time();
		wait_us = accel_interval_ms * MSEC - (ts1.val-ts0.val);

		/*
		 * Guarantee some minimum delay to allow other lower priority
		 * tasks to run.
		 */
		if (wait_us < MIN_MOTION_SENSE_WAIT_TIME)
			wait_us = MIN_MOTION_SENSE_WAIT_TIME;

		task_wait_event(wait_us);
	}
}

/*****************************************************************************/
/* Host commands */

/* Function to map host sensor IDs to motion sensor. */
static struct motion_sensor_t
	*host_sensor_id_to_motion_sensor(int host_id)
{
	int i;
	struct motion_sensor_t *sensor = NULL;

	for (i = 0; i < motion_sensor_count; ++i) {

		sensor = &motion_sensors[i];

		if ((LOCATION_BASE == sensor->location)
			&& (SENSOR_ACCELEROMETER == sensor->type)
			&& (host_id == EC_MOTION_SENSOR_ACCEL_BASE)) {
			break;
		}

		if ((LOCATION_LID == sensor->location)
			&& (SENSOR_ACCELEROMETER == sensor->type)
			&& (host_id == EC_MOTION_SENSOR_ACCEL_LID)) {
			break;
		}

		if ((LOCATION_BASE == sensor->location)
			&& (SENSOR_GYRO == sensor->type)
			&& (host_id == EC_MOTION_SENSOR_GYRO)) {
			break;
		}
	}

	if (i == motion_sensor_count)
		return NULL;

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
	int i, data, ret = EC_RES_INVALID_PARAM;

	switch (in->cmd) {
	case MOTIONSENSE_CMD_DUMP:
		out->dump.module_flags =
			(*(host_get_memmap(EC_MEMMAP_ACC_STATUS)) &
				EC_MEMMAP_ACC_STATUS_PRESENCE_BIT) ?
					MOTIONSENSE_MODULE_FLAG_ACTIVE : 0;

		for (i = 0; i < motion_sensor_count; i++) {
			sensor = &motion_sensors[i];
			out->dump.sensor_flags[i] =
				MOTIONSENSE_SENSOR_FLAG_PRESENT;
			out->dump.data[0+3*i] = sensor->xyz[X];
			out->dump.data[1+3*i] = sensor->xyz[Y];
			out->dump.data[2+3*i] = sensor->xyz[Z];
		}

		args->response_size = sizeof(out->dump);
		break;

	case MOTIONSENSE_CMD_INFO:
		sensor = host_sensor_id_to_motion_sensor(
			in->sensor_odr.sensor_num);

		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;

		if (sensor->type == SENSOR_ACCELEROMETER)
			out->info.type = MOTIONSENSE_TYPE_ACCEL;

		else if (sensor->type == SENSOR_GYRO)
			out->info.type = MOTIONSENSE_TYPE_GYRO;

		if (sensor->location == LOCATION_BASE)
			out->info.location = MOTIONSENSE_LOC_BASE;

		else if (sensor->location == LOCATION_LID)
			out->info.location = MOTIONSENSE_LOC_LID;

		if (sensor->chip == SENSOR_CHIP_KXCJ9)
			out->info.chip = MOTIONSENSE_CHIP_KXCJ9;

		if (sensor->chip == SENSOR_CHIP_LSM6DS0)
			out->info.chip = MOTIONSENSE_CHIP_LSM6DS0;

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
			if (data > MAX_POLLING_INTERVAL_MS)
				data = MAX_POLLING_INTERVAL_MS;

			accel_interval_ap_on_ms = data;
			accel_interval_ms = data;
		}

		out->ec_rate.ret = accel_interval_ap_on_ms;

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

		sensor->drv->get_data_rate(sensor, &data);

		/* Save configuration parameter: ODR */
		sensor->odr = data;
		out->sensor_odr.ret = data;

		args->response_size = sizeof(out->sensor_odr);
		break;

	case MOTIONSENSE_CMD_SENSOR_RANGE:
		/* Verify sensor number is valid. */
		sensor = host_sensor_id_to_motion_sensor(
			in->sensor_odr.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;

		/* Set new data rate if the data arg has a value. */
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
		sensor->range = data;

		out->sensor_range.ret = data;
		args->response_size = sizeof(out->sensor_range);
		break;
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
		     EC_VER_MASK(0));

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
	int id, x, y, z, n = 1;
	struct motion_sensor_t *sensor;

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
		sensor->drv->read(sensor, &x, &y, &z);
		ccprintf("Current raw data %d: %-5d %-5d %-5d\n", id, x, y, z);
		ccprintf("Last calib. data %d: %-5d %-5d %-5d\n", id,
			 sensor->xyz[X], sensor->xyz[Y], sensor->xyz[Z]);
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

	ccprintf("%s\n", sensor->name);
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

		accel_interval_ms = val;
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelinfo, command_display_accel_info,
	"on/off [interval]",
	"Print motion sensor info, lid angle calculations"
	" and set calculation frequency.", NULL);
#endif /* CONFIG_CMD_ACCEL_INFO */

#ifdef CONFIG_ACCEL_INTERRUPTS
/* TODO(crosbug.com/p/426659): this code is broken, does not compile. */
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

#endif /* CONFIG_CMD_ACCELS */


