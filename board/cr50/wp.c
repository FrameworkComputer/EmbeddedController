/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "registers.h"
#include "task.h"
#include "timer.h"

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
			"[<BOOLEAN>]",
			"Get/set the flash HW write-protect signal");


/* When the system is locked down, provide a means to unlock it */
#ifdef CONFIG_RESTRICTED_CONSOLE_COMMANDS

/* TODO(crosbug.com/p/49959): Do the real thing, not this */
static int do_the_dance_for_long_enough(void)
{
	int i;

	ccprintf("Dancing:");
	for (i = 5; i; i--) {
		msleep(500);
		ccprintf(" %d", i);
	}
	msleep(500);
	ccprintf(" done!\n");

	return EC_SUCCESS;
}

/* TODO(crosbug.com/p/55510): It should be locked by default */
static int console_restricted_state;

int console_is_restricted(void)
{
	return console_restricted_state;
}

static int command_lock(int argc, char **argv)
{
	int rc = EC_SUCCESS;
	int enabled;

	if (argc > 1) {
		if (!parse_bool(argv[1], &enabled))
			return EC_ERROR_PARAM1;

		/* Changing nothing does nothing */
		if (enabled == console_restricted_state)
			goto out;

		/* Entering restricted mode is always allowed */
		if (enabled)  {
			console_restricted_state = 1;
			goto out;
		}

		/*
		 * TODO(crosbug.com/p/55322, crosbug.com/p/55728): There may be
		 * other preconditions which must be satisified before
		 * continuing. We can return EC_ERROR_ACCESS_DENIED if those
		 * aren't met.
		 */

		/* Now the user has to sit there and poke the button */
		rc = do_the_dance_for_long_enough();
		if (rc == EC_SUCCESS)
			console_restricted_state = 0;
	}

out:
	ccprintf("The restricted console lock is %s\n",
		 console_is_restricted() ? "enabled" : "disabled");

	return rc;
}
DECLARE_SAFE_CONSOLE_COMMAND(lock, command_lock,
			     "[<BOOLEAN>]",
			     "Get/Set the restricted console lock");

#endif	/* CONFIG_RESTRICTED_CONSOLE_COMMANDS */
