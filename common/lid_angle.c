/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Lid angle module for Chrome EC */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "lid_angle.h"
#include "lid_switch.h"
#include "math_util.h"
#include "motion_lid.h"
#include "motion_sense.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_LIDANGLE, outstr)
#define CPRINTS(format, args...) cprints(CC_LIDANGLE, format, ## args)

/*
 * Define the number of previous lid angle measurements to keep for determining
 * whether to enable or disable peripherals that are only needed for laptop
 * mode. These incude keyboard and trackpad. Note, that in order to change the
 * enable/disable state of these peripherals, all stored measurements of the
 * lid angle buffer must be in the specified range.
 */
#define LID_ANGLE_BUFFER_SIZE 4

/*
 * Define two variables to determine if wake source peripherals that are only
 * applicable for laptop mode should be enabled or disabled in S3 based on the
 * current lid angle. Note, the lid angle is bound to [0, 360]. Here are two
 * angles, defined such that we segregate the lid angle space into two regions.
 * The first region is the region in which we enable peripherals in S3 and is
 * when the lid angle CCW of the small_angle and CW of the large_angle. The
 * second region is the region in which we disable peripherals in S3 and is when
 * the lid angle is CCW of the large_angle and CW of the small_angle.
 *
 * Note, the most sensical values are small_angle = 0 and large_angle = 180,
 * but, the angle measurement is not perfect, and we know that if the angle is
 * near 0 and the lid isn't closed, then the lid must be near 360. So, the
 * small_angle is set to a small positive value to make sure we don't swap modes
 * when the lid is open all the way but is measuring a small positive value.
 */
static int wake_large_angle = 180;
static const int wake_small_angle = 13;

/* Define hysteresis value to add stability to the flags. */
#define LID_ANGLE_HYSTERESIS_DEG  2

/* Define max and min values for wake_large_angle. */
#define LID_ANGLE_MIN_LARGE_ANGLE 0
#define LID_ANGLE_MAX_LARGE_ANGLE 360

/**
 * Determine if given angle is in region to enable peripherals.
 *
 * @param ang Some lid angle in degrees [0, 360]
 *
 * @return true/false
 */
static int lid_in_range_to_enable_peripherals(int ang)
{
	/*
	 * If the wake large angle is min or max, then this function should
	 * return false or true respectively, independent of input angle.
	 */
	if (wake_large_angle == LID_ANGLE_MIN_LARGE_ANGLE)
		return 0;
	else if (wake_large_angle == LID_ANGLE_MAX_LARGE_ANGLE)
		return 1;

	return  (ang >= (wake_small_angle + LID_ANGLE_HYSTERESIS_DEG)) &&
		(ang <= (wake_large_angle - LID_ANGLE_HYSTERESIS_DEG));
}

/**
 * Determine if given angle is in region to ignore peripherals.
 *
 * @param ang Some lid angle in degrees [0, 360]
 *
 * @return true/false
 */
static int lid_in_range_to_ignore_peripherals(int ang)
{
	/*
	 * If the wake large angle is min or max, then this function should
	 * return true or false respectively, independent of input angle.
	 */
	if (wake_large_angle == LID_ANGLE_MIN_LARGE_ANGLE)
		return 1;
	else if (wake_large_angle == LID_ANGLE_MAX_LARGE_ANGLE)
		return 0;

	return  (ang <= (wake_small_angle - LID_ANGLE_HYSTERESIS_DEG)) ||
		(ang >= (wake_large_angle + LID_ANGLE_HYSTERESIS_DEG));
}


int lid_angle_get_wake_angle(void)
{
	return wake_large_angle;
}

void lid_angle_set_wake_angle(int ang)
{
	if (ang < LID_ANGLE_MIN_LARGE_ANGLE)
		ang = LID_ANGLE_MIN_LARGE_ANGLE;
	else if (ang > LID_ANGLE_MAX_LARGE_ANGLE)
		ang = LID_ANGLE_MAX_LARGE_ANGLE;

	wake_large_angle = ang;
}

void lid_angle_update(int lid_ang)
{
	static int lidangle_buffer[LID_ANGLE_BUFFER_SIZE];
	static int index;
	int i;
	int accept = 1, ignore = 1;

	/* Record most recent lid angle in circular buffer. */
	lidangle_buffer[index] = lid_ang;
	index = (index == LID_ANGLE_BUFFER_SIZE-1) ? 0 : index+1;

	/*
	 * Manage whether or not peripherals are enabled based on lid angle
	 * history.
	 */
	for (i = 0; i < LID_ANGLE_BUFFER_SIZE; i++) {
		/*
		 * If any lid angle samples are unreliable, then
		 * don't change peripheral state.
		 */
		if (lidangle_buffer[i] == LID_ANGLE_UNRELIABLE)
			return;

		/*
		 * Force all elements of the lid angle buffer to be
		 * in range of one of the conditions in order to change
		 * to the corresponding peripheral state.
		 */
		if (!lid_in_range_to_enable_peripherals(lidangle_buffer[i]))
			accept = 0;
		if (!lid_in_range_to_ignore_peripherals(lidangle_buffer[i]))
			ignore = 0;
	}

	/* Enable or disable peripherals as necessary. */
	if (accept)
		lid_angle_peripheral_enable(1);
	else if (ignore && !accept)
		lid_angle_peripheral_enable(0);
}

static void enable_peripherals(void)
{
	/*
	 * Make sure lid angle is not disabling peripherals when AP is running.
	 */
	lid_angle_peripheral_enable(1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, enable_peripherals, HOOK_PRIO_DEFAULT);
