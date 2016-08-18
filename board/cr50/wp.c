/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "registers.h"

static int command_wp(int argc, char **argv)
{
	int val;

	if (argc > 1) {
		if (!parse_bool(argv[1], &val))
			return EC_ERROR_PARAM1;

		/* Invert, because active low */
		GREG32(RBOX, EC_WP_L) = !val;
	}

	/* Invert, because active low */
	val = !GREG32(RBOX, EC_WP_L);

	ccprintf("Flash WP is %s\n", val ? "enabled" : "disabled");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(wp, command_wp,
	"[bool]",
	"Get/set the flash HW write-protect signal",
	NULL);
