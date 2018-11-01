/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Motion sense module to read from various motion sensors. */

#include "acpi.h"
#include "accelgyro.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gesture.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_angle.h"
#include "lid_switch.h"
#include "math_util.h"
#include "motion_lid.h"
#include "motion_sense.h"
#include "power.h"
#include "tablet_mode.h"
#include "timer.h"
#include "task.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_MOTION_LID, outstr)
#define CPRINTS(format, args...) cprints(CC_MOTION_LID, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_MOTION_LID, format, ## args)

#ifdef CONFIG_LID_ANGLE_INVALID_CHECK
/* Previous lid_angle. */
static fp_t last_lid_angle_fp = FLOAT_TO_FP(-1);

/*
 * This defines the range from 0 to SMALL_LID_ANGLE_RANGE of possible lid angle
 * measurements when the lid is physically closed.  This will be used in
 * reliability calculations.
 */
#define SMALL_LID_ANGLE_RANGE 15
#endif

/* Current acceleration vectors and current lid angle. */
static int lid_angle_deg;

static int lid_angle_is_reliable;

/*
 * Angle threshold for how close the hinge aligns with gravity before
 * considering the lid angle calculation unreliable. For computational
 * efficiency, value is given unit-less, so if you want the threshold to be
 * at 15 degrees, the value would be cos(15 deg) = 0.96593.
 *
 * Here we're using cos(27.5 deg) = 0.88701.
 */
#define HINGE_ALIGNED_WITH_GRAVITY_THRESHOLD FLOAT_TO_FP(0.88701)

/*
 * Constant to debounce lid angle changes around 360 - 0:
 * If we have a rotation  through the angle 0, ignore.
 */
#define DEBOUNCE_ANGLE_DELTA FLOAT_TO_FP(45)

/*
 * Since the accelerometers are on the same physical device, they should be
 * under the same acceleration.  This constant, which mirrors
 * kNoisyMagnitudeDeviation used in Chromium, is an integer which defines the
 * maximum deviation in magnitude between the base and lid vectors.  The units
 * are in m/s^2.
 */
#define NOISY_MAGNITUDE_DEVIATION 1

/*
 * Define the accelerometer orientation matrices based on the standard
 * reference frame in use (note: accel data is converted to standard ref
 * frame before calculating lid angle).
 */
#ifdef CONFIG_ACCEL_STD_REF_FRAME_OLD
const struct accel_orientation acc_orient = {
	/* Hinge aligns with y axis. */
	.rot_hinge_90 = {
		{ 0,  0,  FLOAT_TO_FP(1)},
		{ 0,  FLOAT_TO_FP(1),  0},
		{ FLOAT_TO_FP(-1), 0,  0}
	},
	.rot_hinge_180 = {
		{ FLOAT_TO_FP(-1), 0,  0},
		{ 0,  FLOAT_TO_FP(1),  0},
		{ 0,  0, FLOAT_TO_FP(-1)}
	},
	.hinge_axis = {0, 1, 0},
};
#else
const struct accel_orientation acc_orient = {
	/* Hinge aligns with x axis. */
	.rot_hinge_90 = {
		{ FLOAT_TO_FP(1),  0,  0},
		{ 0,  0,  FLOAT_TO_FP(1)},
		{ 0, FLOAT_TO_FP(-1),  0}
	},
	.rot_hinge_180 = {
		{ FLOAT_TO_FP(1),  0,  0},
		{ 0, FLOAT_TO_FP(-1),  0},
		{ 0,  0, FLOAT_TO_FP(-1)}
	},
	.hinge_axis = {1, 0, 0},
};
#endif

/* Pointer to constant acceleration orientation data. */
const struct accel_orientation * const p_acc_orient = &acc_orient;

const struct motion_sensor_t * const accel_base =
	&motion_sensors[CONFIG_LID_ANGLE_SENSOR_BASE];
const struct motion_sensor_t * const accel_lid =
	&motion_sensors[CONFIG_LID_ANGLE_SENSOR_LID];

__attribute__((weak)) int board_is_lid_angle_tablet_mode(void)
{
#ifdef CONFIG_LID_ANGLE_TABLET_MODE
	return 1;
#else
	return 0;
#endif
}

#ifdef CONFIG_LID_ANGLE_TABLET_MODE
#ifndef CONFIG_LID_ANGLE_INVALID_CHECK
#error "Check for invalid transition needed"
#endif
/*
 * We are in tablet mode when the lid angle has been calculated
 * to be large.
 *
 * By default, at boot, we are in tablet mode.
 * Once a lid angle is calculated, we will get out of this fake state and enter
 * tablet mode only if a high angle has been calculated.
 *
 * There might be false positives:
 * - when the EC enters RO or RW mode.
 * - when lid is closed while the hinge is perpendicular to the floor, we will
 *   stay in tablet mode.
 *
 * Tablet mode is defined as the base being behind the lid. We use 2 threshold
 * to calculate tablet mode:
 * tablet_mode:
 *   1 |                  +-----<----+----------
 *     |                  \/         /\
 *     |                  |          |
 *   0 |------------------------>----+
 *     +------------------+----------+----------+ lid angle
 *     0                 240        300        360
 */
#define TABLET_ZONE_LID_ANGLE FLOAT_TO_FP(300)
#define LAPTOP_ZONE_LID_ANGLE FLOAT_TO_FP(240)

/*
 * We will change our tablet mode status when we are "convinced" that it has
 * changed.  This means we will have to consecutively calculate our new tablet
 * mode while the angle is stable and come to the same conclusion.  The number
 * of consecutive calculations is the debounce count with an interval between
 * readings set by the motion_sense task.  This should avoid spurious forces
 * that may trigger false transitions of the tablet mode switch.
 */
#define TABLET_MODE_DEBOUNCE_COUNT 3

static int motion_lid_set_tablet_mode(int reliable)
{
	static int tablet_mode_debounce_cnt = TABLET_MODE_DEBOUNCE_COUNT;
	const int current_mode = tablet_get_mode();
	int new_mode = current_mode;

	if (reliable) {
		if (last_lid_angle_fp > TABLET_ZONE_LID_ANGLE)
			new_mode = 1;
		else if (last_lid_angle_fp < LAPTOP_ZONE_LID_ANGLE)
			new_mode = 0;

		/* Only change tablet mode if we're sure. */
		if (current_mode != new_mode) {
			if (tablet_mode_debounce_cnt == 0) {
				/* Alright, we're convinced. */
				tablet_mode_debounce_cnt =
					TABLET_MODE_DEBOUNCE_COUNT;
				tablet_set_mode(new_mode);
				return reliable;
			}
			tablet_mode_debounce_cnt--;
			return reliable;
		}
	}

	/*
	 * If we got a reliable measurement that agrees with our current tablet
	 * mode, then reset the debounce counter.  Also, make it harder to leave
	 * tablet mode by resetting the debounce count when we encounter an
	 * unreliable angle when we're already in tablet mode.
	 */
	if (((reliable == 0) && current_mode == 1) ||
	    ((reliable == 1) && (current_mode == new_mode)))
		tablet_mode_debounce_cnt = TABLET_MODE_DEBOUNCE_COUNT;
	return reliable;
}

#endif /* CONFIG_LID_ANGLE_TABLET_MODE */

#if defined(CONFIG_DPTF_MULTI_PROFILE) && \
	defined(CONFIG_DPTF_MOTION_LID_NO_HALL_SENSOR)

/*
 * If CONFIG_DPTF_MULTI_PROFILE is defined by a board, then lid motion driver
 * sets different profile numbers depending upon the current lid
 * angle. Following profiles are currently supported by this driver:
 * 1. Clamshell mode - DPTF_PROFILE_CLAMSHELL
 * 2. 360-degree flipped mode - DPTF_PROFILE_FLIPPED_360_MODE
 *
 * 360-degree flipped mode is defined as the mode with base being behind the
 * lid. We use 2 threshold to calculate this:
 *
 * 360-degree mode
 *   1 |                  +-----<----+----------
 *     |                  \/         /\
 *     |                  |          |
 *   0 |------------------------>----+
 *     +------------------+----------+----------+ lid angle
 *     0                 240        300        360
 */
#define FLIPPED_360_ZONE_LID_ANGLE FLOAT_TO_FP(300)
#define CLAMSHELL_ZONE_LID_ANGLE FLOAT_TO_FP(240)

/*
 * Detection of DPTF profile is very similar to tablet mode detection using
 * debounce counter. This is done to avoid any spurious changes in setting DPTF
 * profile numbers.
 */
#define DPTF_MODE_DEBOUNCE_COUNT 3

static void motion_lid_set_dptf_profile(int reliable)
{
	static int debounce_cnt = DPTF_MODE_DEBOUNCE_COUNT;
	int current_prof = acpi_dptf_get_profile_num();
	int new_prof = current_prof;

	if (reliable) {
		if (last_lid_angle_fp > FLIPPED_360_ZONE_LID_ANGLE)
			new_prof = DPTF_PROFILE_FLIPPED_360_MODE;
		else if (last_lid_angle_fp < CLAMSHELL_ZONE_LID_ANGLE)
			new_prof = DPTF_PROFILE_CLAMSHELL;

		if (current_prof != new_prof) {
			if (debounce_cnt != 0) {
				debounce_cnt--;
				return;
			}

			debounce_cnt = DPTF_MODE_DEBOUNCE_COUNT;
			acpi_dptf_set_profile_num(new_prof);
			return;
		}
	}

	debounce_cnt = DPTF_MODE_DEBOUNCE_COUNT;
}

#endif /* CONFIG_DPTF_MULTI_PROFILE && CONFIG_DPTF_MOTION_LID_NO_HALL_SENSOR */

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
static int calculate_lid_angle(const intv3_t base, const intv3_t lid,
			       int *lid_angle)
{
	intv3_t v;
	fp_t lid_to_base_fp, cos_lid_90, cos_lid_270;
	fp_t lid_to_base, base_to_hinge;
	fp_t denominator;
	int reliable = 1;
	int base_magnitude2, lid_magnitude2;
	int base_range, lid_range, i;
	intv3_t scaled_base, scaled_lid;

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
	if (fp_abs(base_to_hinge) > HINGE_ALIGNED_WITH_GRAVITY_THRESHOLD)
		reliable = 0;

	base_to_hinge = fp_sq(base_to_hinge);

	/* Check divide by 0. */
	denominator = FLOAT_TO_FP(1.0) - base_to_hinge;
	if (fp_abs(denominator) < FLOAT_TO_FP(0.01)) {
		*lid_angle = 0;
		return 0;
	}

	lid_to_base_fp = arc_cos(fp_div(lid_to_base - base_to_hinge,
					denominator));

	/*
	 * The previous calculation actually has two solutions, a positive and
	 * a negative solution. To figure out the sign of the answer, calculate
	 * the cosine of the angle between the actual lid angle and the
	 * estimated vector if the lid were open to 90 deg, cos_lid_90. Also
	 * calculate the cosine of the angle between the actual lid angle and
	 * the estimated vector if the lid were open to 270 deg,
	 * cos_lid_270. The smaller of the two angles represents which one is
	 * closer. If the lid is closer to the estimated 270 degree vector then
	 * the result is negative, otherwise it is positive.
	 */
	rotate(base, p_acc_orient->rot_hinge_90, v);
	cos_lid_90 = cosine_of_angle_diff(v, lid);
	rotate(v, p_acc_orient->rot_hinge_180, v);
	cos_lid_270 = cosine_of_angle_diff(v, lid);

	/*
	 * Note that cos_lid_90 and cos_lid_270 are not in degrees, because
	 * the arc_cos() was never performed. But, since arc_cos() is
	 * monotonically decreasing, we can do this comparison without ever
	 * taking arc_cos(). But, since the function is monotonically
	 * decreasing, the logic of this comparison is reversed.
	 */
	if (cos_lid_270 > cos_lid_90)
		lid_to_base_fp = -lid_to_base_fp;

	/* Place lid angle between 0 and 360 degrees. */
	if (lid_to_base_fp < 0)
		lid_to_base_fp += FLOAT_TO_FP(360);

	/*
	 * Perform some additional reliability checks.
	 *
	 * If the magnitude of the two vectors differ too greatly, then the
	 * readings are unreliable and we can't use them to calculate the lid
	 * angle.
	 */

	/* Scale the vectors by their range. */
	base_range = accel_base->drv->get_range(accel_base);
	lid_range = accel_lid->drv->get_range(accel_lid);

	for (i = X; i <= Z; i++) {
		/*
		 * To increase precision, we'll use 8x the sensor data in the
		 * intermediate calculation.  We would normally divide by 2^15.
		 *
		 * This is safe because even at a range of 8g, calculating the
		 * magnitude squared should still be less than the max of a
		 * 32-bit signed integer.
		 *
		 * The max that base[i] could be is 32768, resulting in a max
		 * value for scaled_base[i] of 640 @ 8g range and force.
		 * Typically our range is set to 2g.
		 */
		scaled_base[i] = base[i] * base_range * 10 >> 12;
		scaled_lid[i] = lid[i] * lid_range * 10 >> 12;
	}

	base_magnitude2 = (scaled_base[X] * scaled_base[X] +
			   scaled_base[Y] * scaled_base[Y] +
			   scaled_base[Z] * scaled_base[Z]) >> 6;
	lid_magnitude2 = (scaled_lid[X] * scaled_lid[X] +
			  scaled_lid[Y] * scaled_lid[Y] +
			  scaled_lid[Z] * scaled_lid[Z]) >> 6;

	/*
	 * Check to see if they differ than more than NOISY_MAGNITUDE_DEVIATION.
	 * If the vectors do, then the measured angle is unreliable.
	 *
	 * Note, that we don't actually have to take the square root to get the
	 * magnitude, but we can work with the magnitudes squared directly as
	 * shown below:
	 *
	 * If A and B are the base and lid magnitudes, and x is the noisy
	 * magnitude deviation:
	 *
	 *          A - B < x
	 *          A^2 - B^2 < x * (A + B)
	 *          A^2 - B^2 < 2 * x * avg(A, B)
	 *
	 * If we assume that the average acceleration should be about 1g, then
	 * we have:
	 *
	 *          (A^2 - B^2) < 2 * 1g * NOISY_MAGNITUDE_DEVIATION
	 */
	if (ABS(base_magnitude2 - lid_magnitude2) >
	    (2 * 10 * NOISY_MAGNITUDE_DEVIATION))
		reliable = 0;

#ifdef CONFIG_LID_ANGLE_INVALID_CHECK
	/* Ignore large angles when the lid is closed. */
	if (!lid_is_open() &&
	    (lid_to_base_fp > FLOAT_TO_FP(SMALL_LID_ANGLE_RANGE)))
		reliable = 0;

	/*
	 * Ignore small angles when the lid is open.
	 *
	 * Note that we're not correcting the angle, but just marking it as
	 * unreliable.  Attempting to correct the angle would cause bad angles
	 * when closing the lid.  However, there is one edge case.  If the
	 * device is suspended in laptop mode, but then is physically placed in
	 * tablet mode, but ALL the angles are read as unreliable, a keypress
	 * may wake us up.  This is because we require at least 4 consecutive
	 * reliable readings over a threshold to disable key scanning.
	 */
	if (lid_is_open() &&
	    (lid_to_base_fp <= FLOAT_TO_FP(SMALL_LID_ANGLE_RANGE)))
		reliable = 0;

	if (reliable) {
		/*
		 * Seed the lid angle now that we have a reliable
		 * measurement.
		 */
		if (last_lid_angle_fp == FLOAT_TO_FP(-1))
			last_lid_angle_fp = lid_to_base_fp;

		/*
		 * If the angle was last seen as really large and now it's quite
		 * small, we may be rotating around from 360->0 so correct it to
		 * be large. But in case that the lid switch is closed, we can
		 * prove the small angle we see is correct so we take the angle
		 * as is.
		 */
		if ((last_lid_angle_fp >=
		     FLOAT_TO_FP(360) - DEBOUNCE_ANGLE_DELTA) &&
		    (lid_to_base_fp <= DEBOUNCE_ANGLE_DELTA) &&
		    (lid_is_open()))
			last_lid_angle_fp = FLOAT_TO_FP(360) - lid_to_base_fp;
		else
			last_lid_angle_fp = lid_to_base_fp;
	}

	/*
	 * Round to nearest int by adding 0.5. Note, only works because lid
	 * angle is known to be positive.
	 */
	*lid_angle = FP_TO_INT(last_lid_angle_fp + FLOAT_TO_FP(0.5));

	if (board_is_lid_angle_tablet_mode())
		reliable = motion_lid_set_tablet_mode(reliable);

#if defined(CONFIG_DPTF_MULTI_PROFILE) && \
	defined(CONFIG_DPTF_MOTION_LID_NO_HALL_SENSOR)
	motion_lid_set_dptf_profile(reliable);
#endif /* CONFIG_DPTF_MULTI_PROFILE && CONFIG_DPTF_MOTION_LID_NO_HALL_SENSOR */

#else    /* CONFIG_LID_ANGLE_INVALID_CHECK */
	*lid_angle = FP_TO_INT(lid_to_base_fp + FLOAT_TO_FP(0.5));
#endif
	return reliable;
}

int motion_lid_get_angle(void)
{
	if (lid_angle_is_reliable)
		return lid_angle_deg;
	else
		return LID_ANGLE_UNRELIABLE;
}

/*
 * Calculate lid angle and massage the results
 */
void motion_lid_calc(void)
{
#ifndef CONFIG_ACCEL_STD_REF_FRAME_OLD
	/*
	 * rotate lid vector by 180 deg to be in the right coordinate frame
	 * because calculate_lid_angle assumes when the lid is closed, that
	 * the lid and base accelerometer data matches
	 */
	intv3_t lid = { accel_lid->xyz[X],
			accel_lid->xyz[Y] * -1,
			accel_lid->xyz[Z] * -1};
	/* Calculate angle of lid accel. */
	lid_angle_is_reliable = calculate_lid_angle(
			accel_base->xyz, lid,
			&lid_angle_deg);
#else
	/* Calculate angle of lid accel. */
	lid_angle_is_reliable = calculate_lid_angle(
			accel_base->xyz, accel_lid->xyz,
			&lid_angle_deg);
#endif

#ifdef CONFIG_LID_ANGLE_UPDATE
	lid_angle_update(motion_lid_get_angle());
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
#ifdef CONFIG_LID_ANGLE_UPDATE
		/* Set new keyboard wake lid angle if data arg has value. */
		if (in->kb_wake_angle.data != EC_MOTION_SENSE_NO_VALUE)
			lid_angle_set_wake_angle(in->kb_wake_angle.data);

		out->kb_wake_angle.ret = lid_angle_get_wake_angle();
#else
		out->kb_wake_angle.ret = 0;
#endif
		args->response_size = sizeof(out->kb_wake_angle);

		break;

	case MOTIONSENSE_CMD_LID_ANGLE:
#ifdef CONFIG_LID_ANGLE
		out->lid_angle.value = motion_lid_get_angle();
		args->response_size = sizeof(out->lid_angle);
#else
		return EC_RES_INVALID_PARAM;
#endif
		break;

	default:
		return EC_RES_INVALID_PARAM;
	}

	return EC_RES_SUCCESS;
}

