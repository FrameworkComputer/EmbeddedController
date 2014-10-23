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
#include "motion_lid.h"
#include "motion_sense.h"
#include "power.h"
#include "timer.h"
#include "task.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_MOTION_LID, outstr)
#define CPRINTS(format, args...) cprints(CC_MOTION_LID, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_MOTION_LID, format, ## args)

/* For vector_3_t, define which coordinates are in which location. */
enum {
	X, Y, Z
};

/* Current acceleration vectors and current lid angle. */
static float lid_angle_deg;
static int lid_angle_is_reliable;

/*
 * Angle threshold for how close the hinge aligns with gravity before
 * considering the lid angle calculation unreliable. For computational
 * efficiency, value is given unit-less, so if you want the threshold to be
 * at 15 degrees, the value would be cos(15 deg) = 0.96593.
 */
#define HINGE_ALIGNED_WITH_GRAVITY_THRESHOLD 0.96593F


/* Pointer to constant acceleration orientation data. */
const struct accel_orientation * const p_acc_orient = &acc_orient;

struct motion_sensor_t *accel_base = &motion_sensors[CONFIG_SENSOR_BASE];
struct motion_sensor_t *accel_lid = &motion_sensors[CONFIG_SENSOR_LID];

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
static int calculate_lid_angle(const vector_3_t base, const vector_3_t lid,
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
	rotate(base, p_acc_orient->rot_hinge_90, v);
	ang_lid_90 = cosine_of_angle_diff(v, lid);
	rotate(v, p_acc_orient->rot_hinge_180, v);
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

	/* Place lid angle between 0 and 360 degrees. */
	if (ang_lid_to_base < 0)
		ang_lid_to_base += 360;

	*lid_angle = ang_lid_to_base;
	return reliable;
}

int motion_lid_get_angle(void)
{
	if (lid_angle_is_reliable)
		/*
		 * Round to nearest int by adding 0.5. Note, only works because
		 * lid angle is known to be positive.
		 */
		return (int)(lid_angle_deg + 0.5F);
	else
		return (int)LID_ANGLE_UNRELIABLE;
}

/*
 * Calculate lid angle and massage the results
 */
void motion_lid_calc(void)
{
	/* Calculate angle of lid accel. */
	lid_angle_is_reliable = calculate_lid_angle(
			accel_base->xyz,
			accel_lid->xyz,
			&lid_angle_deg);

#ifdef CONFIG_LID_ANGLE_KEY_SCAN
	lidangle_keyscan_update(motion_lid_get_angle());
#endif

}

/*****************************************************************************/
/* Host commands */


int host_cmd_motion_lid(struct host_cmd_handler_args *args)
{
	const struct ec_params_motion_sense *in = args->params;
	struct ec_response_motion_sense *out = args->response;

	switch (in->cmd) {
	case MOTIONSENSE_CMD_KB_WAKE_ANGLE:
#ifdef CONFIG_LID_ANGLE_KEY_SCAN
		/* Set new keyboard wake lid angle if data arg has value. */
		if (in->kb_wake_angle.data != EC_MOTION_SENSE_NO_VALUE)
			lid_angle_set_kb_wake_angle(in->kb_wake_angle.data);

		out->kb_wake_angle.ret = lid_angle_get_kb_wake_angle();
#else
		out->kb_wake_angle.ret = 0;
#endif
		args->response_size = sizeof(out->kb_wake_angle);

		break;

	default:
		return EC_RES_INVALID_PARAM;
	}

	return EC_RES_SUCCESS;
}

