/* Copyright 2024 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "lid_angle.h"
#include "motion_lid.h"
#include "tablet_mode.h"

#define CPRINTS(format, args...) cprints(CC_MOTION_SENSE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_MOTION_SENSE, format, ##args)

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
