/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "util.h"

/* This function is assumed to exist, but we don't use it */
void clock_refresh_console_in_use(void)
{
}

/* What to do when we're just waiting */
static enum {
	IDLE_WFI,				/* default */
	IDLE_SLEEP,
	IDLE_DEEP_SLEEP,
	NUM_CHOICES
} idle_action;

static const char const *idle_name[] = {
	"wfi",
	"sleep",
	"deep sleep",
};
BUILD_ASSERT(ARRAY_SIZE(idle_name) == NUM_CHOICES);

static int command_idle(int argc, char **argv)
{
	int c, i;

	if (argc > 1) {
		c = tolower(argv[1][0]);
		for (i = 0; i < ARRAY_SIZE(idle_name); i++)
			if (idle_name[i][0] == c) {
				idle_action = i;
				break;
			}
	}

	ccprintf("idle action: %s\n", idle_name[idle_action]);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(idle, command_idle,
			"[w|s|d]",
			"Set or show the idle action: wfi, sleep, deep sleep",
			NULL);

/* Custom idle task, executed when no tasks are ready to be scheduled. */
void __idle(void)
{
	while (1) {

		/* Don't even bother unless we've enabled it */
		if (idle_action == IDLE_WFI)
			goto wfi;

		/* TODO(wfrichar): sleep/deep-sleep stuff goes here... */

wfi:
		/* Wait for the next irq event. This stops the CPU clock and
		 * may trigger sleep or deep sleep if enabled. */
		asm("wfi");

		/*
		 * TODO: Normal sleep resumes by handling the interrupt, but we
		 * need to clear PMU_LOW_POWER_DIS right away or we might sleep
		 * again by accident. We can't do that here because we don't
		 * get here until the next idle, so we'll have to do it in the
		 * interrupt handler or when task switching. Deep sleep resumes
		 * with a warm boot, which handles it differently.
		 */
	}
}
