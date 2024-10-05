/* Copyright 2024 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "keyboard_scan.h"
#include "lid_angle.h"
#include "motion_lid.h"
#include "tablet_mode.h"

#define CPRINTS(format, args...) cprints(CC_MOTION_SENSE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_MOTION_SENSE, format, ##args)

/* This callback disables keyboard when convertibles are fully open */
__override void lid_angle_peripheral_enable(int enable)
{
	/*
	 * If the lid is in tablet position via other sensors,
	 * ignore the lid angle, which might be faulty then
	 * disable keyboard.
	 */
	if (tablet_get_mode())
		enable = 0;

	/* EC needs to control the keyboard scan with non-ChromeOS systems */
	keyboard_scan_enable(enable, KB_SCAN_DISABLE_LID_ANGLE);
}

static int cmd_lidangle(int argc, const char **argv)
{
	int angle = motion_lid_get_angle();

	if (angle == LID_ANGLE_UNRELIABLE)
		CPRINTS("Lid angle unreliable");
	else
		CPRINTS("Lid angle: %d", angle);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(lidangle, cmd_lidangle,
			"[lidangle]", "print lid angle");
