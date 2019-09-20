/* Copyright 2014 The Chromium OS Authors. All rights reserved.
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

#ifdef CONFIG_TABLET_MODE
/* Previous lid_angle. */
static fp_t last_lid_angle_fp = FLOAT_TO_FP(-1);

/*
 * This defines the range from 0 to SMALL_LID_ANGLE_RANGE of possible lid angle
 * measurements when the lid is physically closed.  This will be used in
 * reliability calculations.
 */
#define SMALL_LID_ANGLE_RANGE (FLOAT_TO_FP(15))
#endif

/* Current acceleration vectors and current lid angle. */
static int lid_angle_deg;

static int lid_angle_is_reliable;

/* Smoothed vectors to increase accurency. */
static intv3_t smoothed_base, smoothed_lid;

/* 8.7 m/s^2 is the the maximum acceleration parallel to the hinge */
#define SCALED_HINGE_VERTICAL_MAXIMUM  \
	((int)((8.7f * MOTION_SCALING_FACTOR) / MOTION_ONE_G))

#define SCALED_HINGE_VERTICAL_SMOOTHING_START \
	((int)((7.0f * MOTION_SCALING_FACTOR) / MOTION_ONE_G))

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
 * are in g. Currently set at 1m/s^2.
 */
#define NOISY_MAGNITUDE_DEVIATION ((int)(MOTION_SCALING_FACTOR / MOTION_ONE_G))

/*
 * Even with noise, any measurement greater than 1g on any axis is not suitable
 * for lid calculation. It means the device is moving.
 * To avoid using 64bits arithmetic, we need to be sure that square of magnitude
 * is less than 1<<31, so magnitude is less sqrt(2)*(1<<15), less than ~40% over
 * 1g. This is way above any usable noise. Assume noise is less than 10%.
 */
#define MOTION_SCALING_AXIS_MAX (MOTION_SCALING_FACTOR * 110)

#define MOTION_SCALING_FACTOR2 (MOTION_SCALING_FACTOR * MOTION_SCALING_FACTOR)

/*
 * Define the accelerometer orientation matrices based on the standard
 * reference frame in use (note: accel data is converted to standard ref
 * frame before calculating lid angle).
 */
#ifdef CONFIG_ACCEL_STD_REF_FRAME_OLD
static const intv3_t hinge_axis = { 0, 1, 0};
#define HINGE_AXIS Y
#else
static const intv3_t hinge_axis = { 1, 0, 0};
#define HINGE_AXIS X
#endif

static const struct motion_sensor_t * const accel_base =
	&motion_sensors[CONFIG_LID_ANGLE_SENSOR_BASE];
static const struct motion_sensor_t * const accel_lid =
	&motion_sensors[CONFIG_LID_ANGLE_SENSOR_LID];

#ifdef CONFIG_TABLET_MODE
__attribute__((weak)) int board_is_lid_angle_tablet_mode(void)
{
	return 1;
}

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
 * Tablet mode is defined as the lid angle being greater than 180 degree(by
 * default). We use 2 threshold to calculate tablet mode:
 * tablet_mode:
 *   1 |            +-----<----+----------
 *     |            \/         /\
 *     |            |          |
 *   0 |------------------>----+
 *     +------------+----------+----------+ lid angle
 *     0           160        200        360
 *
 * Host can configure the threshold to be different than default of 180 +/- 20
 * by using MOTIONSENSE_CMD_TABLET_MODE_LID_ANGLE.
 */

#define DEFAULT_TABLET_MODE_ANGLE	(180)
#define DEFAULT_TABLET_MODE_HYS	(20)

#define TABLET_ZONE_ANGLE(a, h)	((a) + (h))
#define LAPTOP_ZONE_ANGLE(a, h)	((a) - (h))

static fp_t tablet_zone_lid_angle =
	FLOAT_TO_FP(TABLET_ZONE_ANGLE(DEFAULT_TABLET_MODE_ANGLE,
				      DEFAULT_TABLET_MODE_HYS));
static fp_t laptop_zone_lid_angle =
	FLOAT_TO_FP(LAPTOP_ZONE_ANGLE(DEFAULT_TABLET_MODE_ANGLE,
				      DEFAULT_TABLET_MODE_HYS));

static int tablet_mode_lid_angle = DEFAULT_TABLET_MODE_ANGLE;
static int tablet_mode_hys_degree = DEFAULT_TABLET_MODE_HYS;

static void motion_lid_set_tablet_mode(int reliable)
{
	static int tablet_mode_debounce_cnt = TABLET_MODE_DEBOUNCE_COUNT;
	const int current_mode = tablet_get_mode();
	int new_mode = current_mode;

	if (reliable) {
		if (last_lid_angle_fp > tablet_zone_lid_angle)
			new_mode = 1;
		else if (last_lid_angle_fp < laptop_zone_lid_angle)
			new_mode = 0;

		/* Only change tablet mode if we're sure. */
		if (current_mode != new_mode) {
			if (tablet_mode_debounce_cnt == 0) {
				/* Alright, we're convinced. */
				tablet_mode_debounce_cnt =
					TABLET_MODE_DEBOUNCE_COUNT;
				tablet_set_mode(new_mode);
				return;
			}
			tablet_mode_debounce_cnt--;
			return;
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
}

static int lid_angle_set_tablet_mode_threshold(int angle, int hys)
{
	if ((angle == EC_MOTION_SENSE_NO_VALUE) ||
	    (hys == EC_MOTION_SENSE_NO_VALUE))
		return EC_RES_SUCCESS;

	if ((angle < 0) || (hys < 0) || (angle < hys) || ((angle + hys) > 360))
		return EC_RES_INVALID_PARAM;

	tablet_mode_lid_angle = angle;
	tablet_mode_hys_degree = hys;

	tablet_zone_lid_angle = INT_TO_FP(TABLET_ZONE_ANGLE(angle, hys));
	laptop_zone_lid_angle = INT_TO_FP(LAPTOP_ZONE_ANGLE(angle, hys));

	return EC_RES_SUCCESS;
}

#endif /* CONFIG_TABLET_MODE */

#if defined(CONFIG_DPTF_MULTI_PROFILE) && \
	defined(CONFIG_DPTF_MOTION_LID_NO_GMR_SENSOR)

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

#endif /* CONFIG_DPTF_MULTI_PROFILE && CONFIG_DPTF_MOTION_LID_NO_GMR_SENSOR */

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
	intv3_t cross, proj_lid, proj_base, scaled_base, scaled_lid;
	fp_t lid_to_base_fp, smoothed_ratio;
	int base_magnitude2, lid_magnitude2, largest_hinge_accel;
	int reliable = 1, i;

	/*
	 * Scale the vectors by their range, to be able to compare them.
	 * If a single measurement is greated than 1g, we may overflow fixed
	 * point calculation. However, we can exclude such a measurement, it
	 * means the device is in movement and lid angle calculation is not
	 * possible.
	 */
	for (i = X; i <= Z; i++) {
		scaled_base[i] = base[i] *
			accel_base->drv->get_range(accel_base);
		scaled_lid[i] = lid[i] *
			accel_lid->drv->get_range(accel_lid);
		if (ABS(scaled_base[i]) > MOTION_SCALING_AXIS_MAX ||
		    ABS(scaled_lid[i]) > MOTION_SCALING_AXIS_MAX) {
			reliable = 0;
			goto end_calculate_lid_angle;
		}
	}

	/*
	 * Calculate square of vector magnitude in g.
	 * Each entry is guaranteed to be up to +/- 1<<15, so the square will be
	 * less than 1<<30.
	 */
	base_magnitude2 = scaled_base[X] * scaled_base[X] +
		scaled_base[Y] * scaled_base[Y] +
		scaled_base[Z] * scaled_base[Z];
	lid_magnitude2 = scaled_lid[X] * scaled_lid[X] +
		scaled_lid[Y] * scaled_lid[Y] +
		scaled_lid[Z] * scaled_lid[Z];

	/*
	 * Check to see if they differ than more than NOISY_MAGNITUDE_DEVIATION.
	 * If the vectors do, then the measured angle is unreliable.
	 *
	 * Note, that we don't actually have to take the square root to get the
	 * magnitude, but we can work with the magnitudes squared directly as
	 * shown below:
	 *
	 * If A is a magnitudes, and x is the noisy magnitude deviation:
	 *
	 *          0 < 1g - A < x
	 *          0 < 1g^2 - A^2 < x * (A + B)
	 *          0 < 1g^2 - A^2 < 2 * x * avg(A, B)
	 *
	 * If we assume that the average acceleration should be about 1g, then
	 * we have:
	 *
	 *          0 < 1g^2 - A^2 < 2 * 1g * NOISY_MAGNITUDE_DEVIATION
	 */
	if (MOTION_SCALING_FACTOR2 - base_magnitude2 >
	    2 * MOTION_SCALING_FACTOR * NOISY_MAGNITUDE_DEVIATION) {
		reliable = 0;
		goto end_calculate_lid_angle;
	}
	if (MOTION_SCALING_FACTOR2 - lid_magnitude2 >
	    2 * MOTION_SCALING_FACTOR * NOISY_MAGNITUDE_DEVIATION) {
		reliable = 0;
		goto end_calculate_lid_angle;
	}

	largest_hinge_accel = MAX(ABS(scaled_base[HINGE_AXIS]),
				  ABS(scaled_lid[HINGE_AXIS]));

	smoothed_ratio = MAX(INT_TO_FP(0), MIN(INT_TO_FP(1),
		fp_div(INT_TO_FP(largest_hinge_accel -
				 SCALED_HINGE_VERTICAL_SMOOTHING_START),
		       INT_TO_FP(SCALED_HINGE_VERTICAL_MAXIMUM -
				 SCALED_HINGE_VERTICAL_SMOOTHING_START))));

	/* Check hinge is not too vertical */
	if (largest_hinge_accel > SCALED_HINGE_VERTICAL_MAXIMUM) {
		reliable = 0;
		goto end_calculate_lid_angle;
	}

	/* Smooth input to reduce calculation error due to noise. */
	vector_scale(smoothed_base, smoothed_ratio);
	vector_scale(smoothed_lid, smoothed_ratio);
	vector_scale(scaled_base, INT_TO_FP(1) - smoothed_ratio);
	vector_scale(scaled_lid, INT_TO_FP(1) - smoothed_ratio);
	for (i = X; i <= Z; i++) {
		smoothed_base[i] += scaled_base[i];
		smoothed_lid[i] += scaled_lid[i];
	}

	/* Project vectors on the hinge hyperplan, putting smooth ones aside. */
	memcpy(proj_base, smoothed_base, sizeof(intv3_t));
	memcpy(proj_lid, smoothed_lid, sizeof(intv3_t));
	proj_base[HINGE_AXIS] = 0;
	proj_lid[HINGE_AXIS] = 0;

	/* Calculate the clockwise angle */
	lid_to_base_fp = arc_cos(cosine_of_angle_diff(proj_base, proj_lid));
	cross_product(proj_base, proj_lid, cross);

	/*
	 * If the dot product of this cross product is normal, it means that
	 * the shortest angle between |base| and |lid| was counterclockwise
	 * with respect to the surface represented by |hinge_axis| and this
	 * angle must be reversed.
	 */
	if (dot_product(cross, hinge_axis) > 0)
		lid_to_base_fp = FLOAT_TO_FP(360) - lid_to_base_fp;

#ifndef CONFIG_ACCEL_STD_REF_FRAME_OLD
	/*
	 * Angle is between the keyboard and the front of screen: we need to
	 * anlge between keyboard and back of screen:
	 * 180 instead of 0 when lid and base are flat on surface.
	 * 0 instead of 180 when lid is closed on keyboard.
	 */
	lid_to_base_fp = FLOAT_TO_FP(180) - lid_to_base_fp;
#endif

	/* Place lid angle between 0 and 360 degrees. */
	if (lid_to_base_fp < 0)
		lid_to_base_fp += FLOAT_TO_FP(360);

#ifdef CONFIG_TABLET_MODE
	/* Ignore large angles when the lid is closed. */
	if (!lid_is_open() &&
	    (lid_to_base_fp > SMALL_LID_ANGLE_RANGE)) {
		reliable = 0;
		goto end_calculate_lid_angle;
	}

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
	    (lid_to_base_fp <= SMALL_LID_ANGLE_RANGE)) {
		reliable = 0;
		goto end_calculate_lid_angle;
	}

	/* Seed the lid angle now that we have a reliable measurement. */
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

end_calculate_lid_angle:
	/*
	 * Round to nearest int by adding 0.5. Note, only works because lid
	 * angle is known to be positive.
	 */
	*lid_angle = FP_TO_INT(last_lid_angle_fp + FLOAT_TO_FP(0.5));

	if (board_is_lid_angle_tablet_mode())
		motion_lid_set_tablet_mode(reliable);

#if defined(CONFIG_DPTF_MULTI_PROFILE) && \
	defined(CONFIG_DPTF_MOTION_LID_NO_GMR_SENSOR)
	motion_lid_set_dptf_profile(reliable);
#endif /* CONFIG_DPTF_MULTI_PROFILE && CONFIG_DPTF_MOTION_LID_NO_GMR_SENSOR */

#else    /* CONFIG_TABLET_MODE */
end_calculate_lid_angle:
	if (reliable)
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
	/* Calculate angle of lid accel. */
	lid_angle_is_reliable = calculate_lid_angle(
			accel_base->xyz, accel_lid->xyz,
			&lid_angle_deg);

#ifdef CONFIG_LID_ANGLE_UPDATE
	lid_angle_update(motion_lid_get_angle());
#endif
}

/*****************************************************************************/
/* Host commands */


enum ec_status host_cmd_motion_lid(struct host_cmd_handler_args *args)
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

	case MOTIONSENSE_CMD_TABLET_MODE_LID_ANGLE:
		{
#ifdef CONFIG_TABLET_MODE
			int ret;
			ret = lid_angle_set_tablet_mode_threshold(
					in->tablet_mode_threshold.lid_angle,
					in->tablet_mode_threshold.hys_degree);

			if (ret != EC_RES_SUCCESS)
				return ret;

			out->tablet_mode_threshold.lid_angle =
				tablet_mode_lid_angle;
			out->tablet_mode_threshold.hys_degree =
				tablet_mode_hys_degree;

			args->response_size =
				sizeof(out->tablet_mode_threshold);
#else
			return EC_RES_INVALID_PARAM;
#endif
		}
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	return EC_RES_SUCCESS;
}

