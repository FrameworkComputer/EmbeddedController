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
static float lid_angle_deg;

/* Sampling interval for measuring acceleration and calculating lid angle. */
static int accel_interval_ms = 250;

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
 */
static float calculate_lid_angle(vector_3_t base, vector_3_t lid)
{
	vector_3_t v;
	float ang_lid_to_base, ang_lid_90, ang_lid_270;
	float lid_to_base, base_to_hinge;

	/*
	 * The angle between lid and base is:
	 * acos((cad(base, lid) - cad(base, hinge)^2) /(1 - cad(base, hinge)^2))
	 * where cad() is the cosine_of_angle_diff() function.
	 *
	 * Make sure to check for divide by 0.
	 */
	lid_to_base = cosine_of_angle_diff(base, lid);
	base_to_hinge = cosine_of_angle_diff(base, p_acc_orient->hinge_axis);
	base_to_hinge = SQ(base_to_hinge);

	/* Check divide by 0. */
	if (ABS(1.0F - base_to_hinge) < 0.01F)
		return 0.0;

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

	return ang_lid_to_base;
}

int motion_get_lid_angle(void)
{
	return (int)lid_angle_deg;
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


void motion_sense_task(void)
{
	timestamp_t ts0, ts1;
	int wait_us;
	int ret;
	uint8_t *lpc_status;
	uint16_t *lpc_data;
	int sample_id = 0;

	lpc_status = host_get_memmap(EC_MEMMAP_ACC_STATUS);
	lpc_data = (uint16_t *)host_get_memmap(EC_MEMMAP_ACC_DATA);

	/* Initialize accelerometers. */
	ret = accel_init(ACCEL_LID);
	ret |= accel_init(ACCEL_BASE);

	/* If accelerometers do not initialize, then end task. */
	if (ret != EC_SUCCESS) {
		CPRINTF("[%T, Accelerometers failed to initialize. Stopping "
				"motion sense task.\n");
		return;
	}

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
		lid_angle_deg = calculate_lid_angle(acc_base, acc_lid);

		/* TODO(crosbug.com/p/25597): Add filter to smooth lid angle. */

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
		lpc_data[0] = (int)lid_angle_deg;
		lpc_data[1] = acc_base[X];
		lpc_data[2] = acc_base[Y];
		lpc_data[3] = acc_base[Z];
		lpc_data[4] = acc_lid[X];
		lpc_data[5] = acc_lid[Y];
		lpc_data[6] = acc_lid[Z];

		/*
		 * Increment sample id and clear busy bit to signal we finished
		 * updating data.
		 */
		sample_id = (sample_id + 1) &
				EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK;
		*lpc_status = sample_id;


#ifdef CONFIG_CMD_LID_ANGLE
		if (accel_disp) {
			CPRINTF("[%T ACC base=%-5d, %-5d, %-5d  lid=%-5d, "
					"%-5d, %-5d  a=%-6.1d]\n",
					acc_base[X], acc_base[Y], acc_base[Z],
					acc_lid[X], acc_lid[Y], acc_lid[Z],
					(int)(10*lid_angle_deg));
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

	/* Second arg changes the accel task time interval. */
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
