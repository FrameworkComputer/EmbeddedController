/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Lid angle module for Chrome EC */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "keyboard_scan.h"
#include "lid_angle.h"
#include "lid_switch.h"
#include "motion_sense.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_LIDANGLE, outstr)
#define CPRINTF(format, args...) cprintf(CC_LIDANGLE, format, ## args)

/*
 * Define the number of previous lid angle measurements to keep for determining
 * whether to enable or disable key scanning. Note, that in order to change
 * the state of key scanning, all stored measurements of the lid angle buffer
 * must be in the specified range.
 */
#define KEY_SCAN_LID_ANGLE_BUFFER_SIZE 4

void lidangle_keyscan_update(float lid_ang)
{
	static float lidangle_buffer[KEY_SCAN_LID_ANGLE_BUFFER_SIZE];
	static int index;

	int i;
	int keys_accept = 1, keys_ignore = 1;

	/* Record most recent lid angle in circular buffer. */
	lidangle_buffer[index] = lid_ang;
	index = (index == KEY_SCAN_LID_ANGLE_BUFFER_SIZE-1) ? 0 : index+1;

#ifdef CONFIG_LID_SWITCH
	/*
	 * If lid is closed, don't need to check if keyboard scanning should
	 * be enabled.
	 */
	if (!lid_is_open())
		return;
#endif

	if (chipset_in_state(CHIPSET_STATE_SUSPEND)) {
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
			if (!LID_IN_RANGE_TO_ACCEPT_KEYS(lidangle_buffer[i]))
				keys_accept = 0;
			if (!LID_IN_RANGE_TO_IGNORE_KEYS(lidangle_buffer[i]))
				keys_ignore = 0;
		}

		/* Enable or disable keyboard scanning if necessary. */
		if (keys_accept && !keyboard_scan_is_enabled()) {
			CPRINTF("[%T Enabling keyboard scan, lid ang at %d]\n",
					(int)lidangle_buffer[index]);
			keyboard_scan_enable(1);
		} else if (keys_ignore && !keys_accept &&
				keyboard_scan_is_enabled()) {
			CPRINTF("[%T Disabling keyboard scan, lid ang at %d]\n",
					(int)lidangle_buffer[index]);
			keyboard_scan_enable(0);
		}
	}
}
