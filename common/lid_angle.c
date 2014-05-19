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
#include "motion_sense.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_LIDANGLE, outstr)
#define CPRINTS(format, args...) cprints(CC_LIDANGLE, format, ## args)

/*
 * Define the number of previous lid angle measurements to keep for determining
 * whether to enable or disable key scanning. Note, that in order to change
 * the state of key scanning, all stored measurements of the lid angle buffer
 * must be in the specified range.
 */
#define KEY_SCAN_LID_ANGLE_BUFFER_SIZE 4

/*
 * Define two variables to determine if keyboard scanning should be enabled
 * or disabled in S3 based on the current lid angle. Note, the lid angle is
 * bound to [0, 360]. Here are two angles, defined such that we segregate the
 * lid angle space into two regions. The first region is the region in which
 * we enable keyboard scanning in S3 and is when the lid angle CCW of the
 * small_angle and CW of the large_angle. The second region is the region in
 * which we disable keyboard scanning in S3 and is when the lid angle is CCW
 * of the large_angle and CW of the small_angle.
 *
 * Note, the most sensical values are small_angle = 0 and large_angle = 180,
 * but, the angle measurement is not perfect, and we know that if the angle is
 * near 0 and the lid isn't closed, then the lid must be near 360. So, the
 * small_angle is set to a small positive value to make sure we don't swap modes
 * when the lid is open all the way but is measuring a small positive value.
 */
static int kb_wake_large_angle = 180;
static const int kb_wake_small_angle = 13;

/* Define hysteresis value to add stability to the keyboard scanning flag. */
#define KB_DIS_HYSTERESIS_DEG  2

/* Define max and min values for kb_wake_large_angle. */
#define KB_DIS_MIN_LARGE_ANGLE 0
#define KB_DIS_MAX_LARGE_ANGLE 360

/**
 * Determine if given angle is in region to accept keyboard presses.
 *
 * @param ang Some lid angle in degrees [0, 360]
 *
 * @return true/false
 */
static int lid_in_range_to_accept_keys(float ang)
{
	/*
	 * If the keyboard wake large angle is min or max, then this
	 * function should return false or true respectively, independent of
	 * input angle.
	 */
	if (kb_wake_large_angle == KB_DIS_MIN_LARGE_ANGLE)
		return 0;
	else if (kb_wake_large_angle == KB_DIS_MAX_LARGE_ANGLE)
		return 1;

	return  (ang >= (kb_wake_small_angle + KB_DIS_HYSTERESIS_DEG)) &&
		(ang <= (kb_wake_large_angle - KB_DIS_HYSTERESIS_DEG));
}

/**
 * Determine if given angle is in region to ignore keyboard presses.
 *
 * @param ang Some lid angle in degrees [0, 360]
 *
 * @return true/false
 */
static int lid_in_range_to_ignore_keys(float ang)
{
	/*
	 * If the keyboard wake large angle is min or max, then this
	 * function should return true or false respectively, independent of
	 * input angle.
	 */
	if (kb_wake_large_angle == KB_DIS_MIN_LARGE_ANGLE)
		return 1;
	else if (kb_wake_large_angle == KB_DIS_MAX_LARGE_ANGLE)
		return 0;

	return  (ang <= (kb_wake_small_angle - KB_DIS_HYSTERESIS_DEG)) ||
		(ang >= (kb_wake_large_angle + KB_DIS_HYSTERESIS_DEG));
}


int lid_angle_get_kb_wake_angle(void)
{
	return kb_wake_large_angle;
}

void lid_angle_set_kb_wake_angle(int ang)
{
	if (ang < KB_DIS_MIN_LARGE_ANGLE)
		ang = KB_DIS_MIN_LARGE_ANGLE;
	else if (ang > KB_DIS_MAX_LARGE_ANGLE)
		ang = KB_DIS_MAX_LARGE_ANGLE;

	kb_wake_large_angle = ang;
}

void lidangle_keyscan_update(float lid_ang)
{
	static float lidangle_buffer[KEY_SCAN_LID_ANGLE_BUFFER_SIZE];
	static int index;

	int i;
	int keys_accept = 1, keys_ignore = 1;

	/* Record most recent lid angle in circular buffer. */
	lidangle_buffer[index] = lid_ang;
	index = (index == KEY_SCAN_LID_ANGLE_BUFFER_SIZE-1) ? 0 : index+1;

	/*
	 * Any time the chipset is off, manage whether or not keyboard scanning
	 * is enabled based on lid angle history.
	 */
	if (!chipset_in_state(CHIPSET_STATE_ON)) {
		for (i = 0; i < KEY_SCAN_LID_ANGLE_BUFFER_SIZE; i++) {
			/*
			 * If any lid angle samples are unreliable, then
			 * don't change keyboard scanning state.
			 */
			if (lidangle_buffer[i] == LID_ANGLE_UNRELIABLE)
				return;

			/*
			 * Force all elements of the lid angle buffer to be
			 * in range of one of the conditions in order to change
			 * to the corresponding key scanning state.
			 */
			if (!lid_in_range_to_accept_keys(lidangle_buffer[i]))
				keys_accept = 0;
			if (!lid_in_range_to_ignore_keys(lidangle_buffer[i]))
				keys_ignore = 0;
		}

		/* Enable or disable keyboard scanning as necessary. */
		if (keys_accept)
			keyboard_scan_enable(1, KB_SCAN_DISABLE_LID_ANGLE);
		else if (keys_ignore && !keys_accept)
			keyboard_scan_enable(0, KB_SCAN_DISABLE_LID_ANGLE);
	}
}

static void enable_keyboard(void)
{
	/*
	 * Make sure lid angle is not disabling keyboard scanning when AP is
	 * running.
	 */
	keyboard_scan_enable(1, KB_SCAN_DISABLE_LID_ANGLE);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, enable_keyboard, HOOK_PRIO_DEFAULT);
