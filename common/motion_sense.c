/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Motion sense module to read from various motion sensors. */

#include "accelerometer.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_angle.h"
#include "math_util.h"
#include "motion_sense.h"
#include "timer.h"
#include "task.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_MOTION_SENSE, outstr)
#define CPRINTF(format, args...) cprintf(CC_MOTION_SENSE, format, ## args)

/* Minimum time in between running motion sense task loop. */
#define MIN_MOTION_SENSE_WAIT_TIME (1 * MSEC)

/* Current acceleration vectors and current lid angle. */
static vector_3_t acc_lid_raw, acc_lid, acc_base;
static vector_3_t acc_lid_host, acc_base_host;
static float lid_angle_deg;
static int lid_angle_is_reliable;

/* Bounds for setting the sensor polling interval. */
#define MIN_POLLING_INTERVAL_MS 5
#define MAX_POLLING_INTERVAL_MS 1000

/* Accelerometer polling intervals based on chipset state. */
static int accel_interval_ap_on_ms = 10;
static const int accel_interval_ap_suspend_ms = 100;

/*
 * Angle threshold for how close the hinge aligns with gravity before
 * considering the lid angle calculation unreliable. For computational
 * efficiency, value is given unit-less, so if you want the threshold to be
 * at 15 degrees, the value would be cos(15 deg) = 0.96593.
 */
#define HINGE_ALIGNED_WITH_GRAVITY_THRESHOLD 0.96593F

/* Sampling interval for measuring acceleration and calculating lid angle. */
static int accel_interval_ms;

#ifdef CONFIG_CMD_LID_ANGLE
static int accel_disp;
#endif

/* For vector_3_t, define which coordinates are in which location. */
enum {
	X, Y, Z
};

/* Pointer to constant acceleration orientation data. */
const struct accel_orientation * const p_acc_orient = &acc_orient;

/**
 * Calculate the lid angle using two acceleration vectors, one recorded in
 * the base and one in the lid.
 *
 * @param base Base accel vector
 * @param lid  Lid accel vector
 * @param lid_angle Pointer to location to store lid angle result
 *
 * @return flag representing if resulting lid angle calculation is reliable.
 */
static int calculate_lid_angle(vector_3_t base, vector_3_t lid,
		float *lid_angle)
{
	vector_3_t v;
	float ang_lid_to_base, ang_lid_90, ang_lid_270;
	float lid_to_base, base_to_hinge;
	int reliable = 1;

	/*
	 * The angle between lid and base is:
	 * acos((cad(base, lid) - cad(base, hinge)^2) /(1 - cad(base, hinge)^2))
	 * where cad() is the cosine_of_angle_diff() function.
	 *
	 * Make sure to check for divide by 0.
	 */
	lid_to_base = cosine_of_angle_diff(base, lid);
	base_to_hinge = cosine_of_angle_diff(base, p_acc_orient->hinge_axis);

	/*
	 * If hinge aligns too closely with gravity, then result may be
	 * unreliable.
	 */
	if (ABS(base_to_hinge) > HINGE_ALIGNED_WITH_GRAVITY_THRESHOLD)
		reliable = 0;

	base_to_hinge = SQ(base_to_hinge);

	/* Check divide by 0. */
	if (ABS(1.0F - base_to_hinge) < 0.01F) {
		*lid_angle = 0.0;
		return 0;
	}

	ang_lid_to_base = arc_cos(
			(lid_to_base - base_to_hinge) / (1 - base_to_hinge));

	/*
	 * The previous calculation actually has two solutions, a positive and
	 * a negative solution. To figure out the sign of the answer, calculate
	 * the angle between the actual lid angle and the estimated vector if
	 * the lid were open to 90 deg, ang_lid_90. Also calculate the angle
	 * between the actual lid angle and the estimated vector if the lid
	 * were open to 270 deg, ang_lid_270. The smaller of the two angles
	 * represents which one is closer. If the lid is closer to the
	 * estimated 270 degree vector then the result is negative, otherwise
	 * it is positive.
	 */
	rotate(base, &p_acc_orient->rot_hinge_90, &v);
	ang_lid_90 = cosine_of_angle_diff(v, lid);
	rotate(v, &p_acc_orient->rot_hinge_180, &v);
	ang_lid_270 = cosine_of_angle_diff(v, lid);

	/*
	 * Note that ang_lid_90 and ang_lid_270 are not in degrees, because
	 * the arc_cos() was never performed. But, since arc_cos() is
	 * monotonically decreasing, we can do this comparison without ever
	 * taking arc_cos(). But, since the function is monotonically
	 * decreasing, the logic of this comparison is reversed.
	 */
	if (ang_lid_270 > ang_lid_90)
		ang_lid_to_base = -ang_lid_to_base;

	*lid_angle = ang_lid_to_base;
	return reliable;
}

int motion_get_lid_angle(void)
{
	if (lid_angle_is_reliable)
		return (int)lid_angle_deg;
	else
		return (int)LID_ANGLE_UNRELIABLE;
}

#ifdef CONFIG_ACCEL_CALIBRATE
void motion_get_accel_lid(vector_3_t *v, int adjusted)
{
	memcpy(v, adjusted ? &acc_lid : &acc_lid_raw, sizeof(vector_3_t));
}

void motion_get_accel_base(vector_3_t *v)
{
	memcpy(v, &acc_base, sizeof(vector_3_t));
}
#endif

static void set_ap_suspend_polling(void)
{
	accel_interval_ms = accel_interval_ap_suspend_ms;
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, set_ap_suspend_polling, HOOK_PRIO_DEFAULT);

static void set_ap_on_polling(void)
{
	accel_interval_ms = accel_interval_ap_on_ms;
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, set_ap_on_polling, HOOK_PRIO_DEFAULT);


void motion_sense_task(void)
{
	static timestamp_t ts0, ts1;
	int wait_us;
	int ret;
	uint8_t *lpc_status;
	uint16_t *lpc_data;
	int sample_id = 0;

	lpc_status = host_get_memmap(EC_MEMMAP_ACC_STATUS);
	lpc_data = (uint16_t *)host_get_memmap(EC_MEMMAP_ACC_DATA);

	/*
	 * TODO(crosbug.com/p/27320): The motion_sense task currently assumes
	 * one configuration of motion sensors. Namely, it assumes there is
	 * one accel in the base, one in the lid, and they both use the same
	 * driver. Eventually, all of these assumptions will have to be removed
	 * when we have other configurations of motion sensors.
	 */

	/* Initialize accelerometers. */
	ret = accel_init(ACCEL_LID);
	ret |= accel_init(ACCEL_BASE);

	/* If accelerometers do not initialize, then end task. */
	if (ret != EC_SUCCESS) {
		CPRINTF("[%T, Accelerometers failed to initialize. Stopping "
				"motion sense task.\n");
		return;
	}

	/* Initialize sampling interval. */
	accel_interval_ms = accel_interval_ap_suspend_ms;

	/* Set default accelerometer parameters. */
	accel_set_range(ACCEL_LID,  2, 1);
	accel_set_range(ACCEL_BASE, 2, 1);
	accel_set_resolution(ACCEL_LID,  12, 1);
	accel_set_resolution(ACCEL_BASE, 12, 1);
	accel_set_datarate(ACCEL_LID,  100000, 1);
	accel_set_datarate(ACCEL_BASE, 100000, 1);

	/* Write to status byte to represent that accelerometers are present. */
	*lpc_status |= EC_MEMMAP_ACC_STATUS_PRESENCE_BIT;

	while (1) {
		ts0 = get_time();

		/* Read all accelerations. */
		accel_read(ACCEL_LID, &acc_lid_raw[X], &acc_lid_raw[Y],
			   &acc_lid_raw[Z]);
		accel_read(ACCEL_BASE, &acc_base[X], &acc_base[Y],
			   &acc_base[Z]);

		/*
		 * Rotate the lid vector so the reference frame aligns with
		 * the base sensor.
		 */
		rotate(acc_lid_raw, &p_acc_orient->rot_align, &acc_lid);

		/* Calculate angle of lid. */
		lid_angle_is_reliable = calculate_lid_angle(acc_base, acc_lid,
				&lid_angle_deg);

		/* TODO(crosbug.com/p/25597): Add filter to smooth lid angle. */

		/* Rotate accels into standard reference frame for the host. */
		rotate(acc_base, &p_acc_orient->rot_standard_ref,
				&acc_base_host);
		rotate(acc_lid, &p_acc_orient->rot_standard_ref,
				&acc_lid_host);

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
		 * assumes little endian, which is what the host expects.
		 */
		lpc_data[0] = motion_get_lid_angle();
		lpc_data[1] = acc_base_host[X];
		lpc_data[2] = acc_base_host[Y];
		lpc_data[3] = acc_base_host[Z];
		lpc_data[4] = acc_lid_host[X];
		lpc_data[5] = acc_lid_host[Y];
		lpc_data[6] = acc_lid_host[Z];

		/*
		 * Increment sample id and clear busy bit to signal we finished
		 * updating data.
		 */
		sample_id = (sample_id + 1) &
				EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK;
		*lpc_status = EC_MEMMAP_ACC_STATUS_PRESENCE_BIT | sample_id;

#ifdef CONFIG_LID_ANGLE_KEY_SCAN
		lidangle_keyscan_update(motion_get_lid_angle());
#endif

#ifdef CONFIG_CMD_LID_ANGLE
		if (accel_disp) {
			CPRINTF("[%T ACC base=%-5d, %-5d, %-5d  lid=%-5d, "
					"%-5d, %-5d  a=%-6.1d r=%d]\n",
					acc_base[X], acc_base[Y], acc_base[Z],
					acc_lid[X], acc_lid[Y], acc_lid[Z],
					(int)(10*lid_angle_deg),
					lid_angle_is_reliable);
		}
#endif

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

void accel_int_lid(enum gpio_signal signal)
{
	/*
	 * Print statement is here for testing with console accelint command.
	 * Remove print statement when interrupt is used for real.
	 */
	CPRINTF("[%T Accelerometer wake-up interrupt occurred on lid]\n");
}

void accel_int_base(enum gpio_signal signal)
{
	/*
	 * Print statement is here for testing with console accelint command.
	 * Remove print statement when interrupt is used for real.
	 */
	CPRINTF("[%T Accelerometer wake-up interrupt occurred on base]\n");
}

/*****************************************************************************/
/* Host commands */

/**
 * Temporary function to map host sensor IDs to EC sensor IDs.
 *
 * TODO(crosbug.com/p/27320): Eventually we need a board specific table
 * specifying which motion sensors are attached and which driver to use to
 * access that sensor. Once we have this, this function should be able to go
 * away.
 */
static int host_sensor_id_to_ec_sensor_id(int host_id)
{
	switch (host_id) {
	case EC_MOTION_SENSOR_ACCEL_BASE:
		return ACCEL_BASE;
	case EC_MOTION_SENSOR_ACCEL_LID:
		return ACCEL_LID;
	}

	/* If no match then the EC currently doesn't support ID received. */
	return -1;
}

static int host_cmd_motion_sense(struct host_cmd_handler_args *args)
{
	const struct ec_params_motion_sense *in = args->params;
	struct ec_response_motion_sense *out = args->response;
	int id, data;

	switch (in->cmd) {
	case MOTIONSENSE_CMD_DUMP:
		/*
		 * TODO(crosbug.com/p/27320): Need to remove hard coding and
		 * use some motion_sense data structure from the board file to
		 * help fill in this response.
		 */
		out->dump.module_flags =
			(*(host_get_memmap(EC_MEMMAP_ACC_STATUS)) &
				EC_MEMMAP_ACC_STATUS_PRESENCE_BIT) ?
					MOTIONSENSE_MODULE_FLAG_ACTIVE : 0;
		out->dump.sensor_flags[0] = MOTIONSENSE_SENSOR_FLAG_PRESENT;
		out->dump.sensor_flags[1] = MOTIONSENSE_SENSOR_FLAG_PRESENT;
		out->dump.sensor_flags[2] = 0;
		out->dump.data[0] = acc_base_host[X];
		out->dump.data[1] = acc_base_host[Y];
		out->dump.data[2] = acc_base_host[Z];
		out->dump.data[3] = acc_lid_host[X];
		out->dump.data[4] = acc_lid_host[Y];
		out->dump.data[5] = acc_lid_host[Z];

		args->response_size = sizeof(out->dump);
		break;

	case MOTIONSENSE_CMD_INFO:
		/*
		 * TODO(crosbug.com/p/27320): Need to remove hard coding and
		 * use some motion_sense data structure from the board file to
		 * help fill in this response.
		 */
		id = host_sensor_id_to_ec_sensor_id(in->sensor_odr.sensor_num);
		if (id < 0)
			return EC_RES_INVALID_PARAM;

		switch (id) {
		case ACCEL_BASE:
			out->info.type = MOTIONSENSE_TYPE_ACCEL;
			out->info.location = MOTIONSENSE_LOC_BASE;
			out->info.chip = MOTIONSENSE_CHIP_KXCJ9;
			break;
		case ACCEL_LID:
			out->info.type = MOTIONSENSE_TYPE_ACCEL;
			out->info.location = MOTIONSENSE_LOC_LID;
			out->info.chip = MOTIONSENSE_CHIP_KXCJ9;
			break;
		default:
			return EC_RES_INVALID_PARAM;
		}

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
		id = host_sensor_id_to_ec_sensor_id(in->sensor_odr.sensor_num);
		if (id < 0)
			return EC_RES_INVALID_PARAM;

		/* Set new datarate if the data arg has a value. */
		if (in->sensor_odr.data != EC_MOTION_SENSE_NO_VALUE) {
			if (accel_set_datarate(id, in->sensor_odr.data,
					in->sensor_odr.roundup) != EC_SUCCESS) {
				CPRINTF("[%T MS bad sensor rate %d]\n",
						in->sensor_odr.data);
				return EC_RES_INVALID_PARAM;
			}
		}

		accel_get_datarate(id, &data);
		out->sensor_odr.ret = data;

		args->response_size = sizeof(out->sensor_odr);
		break;

	case MOTIONSENSE_CMD_SENSOR_RANGE:
		/* Verify sensor number is valid. */
		id = host_sensor_id_to_ec_sensor_id(in->sensor_odr.sensor_num);
		if (id < 0)
			return EC_RES_INVALID_PARAM;

		/* Set new datarate if the data arg has a value. */
		if (in->sensor_range.data != EC_MOTION_SENSE_NO_VALUE) {
			if (accel_set_range(id, in->sensor_range.data,
				in->sensor_range.roundup) != EC_SUCCESS) {
				CPRINTF("[%T MS bad sensor range %d]\n",
						in->sensor_range.data);
				return EC_RES_INVALID_PARAM;
			}
		}

		accel_get_range(id, &data);
		out->sensor_range.ret = data;

		args->response_size = sizeof(out->sensor_range);
		break;

	default:
		CPRINTF("[%T MS bad cmd 0x%x]\n", in->cmd);
		return EC_RES_INVALID_PARAM;
	}

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_MOTION_SENSE_CMD,
		     host_cmd_motion_sense,
		     EC_VER_MASK(0));

/*****************************************************************************/
/* Console commands */
#ifdef CONFIG_CMD_LID_ANGLE
static int command_ctrl_print_lid_angle_calcs(int argc, char **argv)
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
DECLARE_CONSOLE_COMMAND(lidangle, command_ctrl_print_lid_angle_calcs,
	"on/off [interval]",
	"Print lid angle calculations and set calculation frequency.", NULL);
#endif /* CONFIG_CMD_LID_ANGLE */
