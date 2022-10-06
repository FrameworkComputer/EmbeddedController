/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* This provides the interface for any Ambient Light Sensors that are connected
 * to the EC instead of the AP.
 */

#include "als.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_ALS, outstr)
#define CPRINTS(format, args...) cprints(CC_ALS, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_ALS, format, ## args)

#ifndef ALS_POLL_PERIOD
#define ALS_POLL_PERIOD SECOND
#endif

static int task_timeout = -1;

int als_read(enum als_id id, int *lux)
{
	int af = als[id].attenuation_factor;
	return als[id].read(lux, af);
}

void als_task(void *u)
{
	int i, val;
	uint16_t *mapped = (uint16_t *)host_get_memmap(EC_MEMMAP_ALS);
	uint16_t als_data;

	while (1) {
		task_wait_event(task_timeout);

		/* If task was disabled while waiting do not read from ALS */
		if (task_timeout < 0)
			continue;

		for (i = 0; i < EC_ALS_ENTRIES && i < ALS_COUNT; i++) {
			als_data = als_read(i, &val) == EC_SUCCESS ? val : 0;
			mapped[i] = als_data;
		}
	}
}

static void als_task_enable(void)
{
	int fail_count = 0;
	int err;
	int i;

	for (i = 0; i < EC_ALS_ENTRIES && i < ALS_COUNT; i++) {
		err = als[i].init();
		if (err) {
			fail_count++;
			CPRINTF("%s ALS sensor failed to initialize, err=%d\n",
				als[i].name, err);
		}
	}

	/*
	 * If all the ALS filed to initialize, disable the ALS task.
	 */
	if (fail_count == ALS_COUNT)
		task_timeout = -1;
	else
		task_timeout = ALS_POLL_PERIOD;

	task_wake(TASK_ID_ALS);
}

static void als_task_disable(void)
{
	task_timeout = -1;
}

static void als_task_init(void)
{
	/*
	 * Enable ALS task in S0 only and may need to re-enable
	 * when sysjumped.
	 */
	if (system_jumped_late() &&
		chipset_in_state(CHIPSET_STATE_ON))
		als_task_enable();
}

DECLARE_HOOK(HOOK_CHIPSET_RESUME, als_task_enable, HOOK_PRIO_ALS_INIT);
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, als_task_disable, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_INIT, als_task_init, HOOK_PRIO_ALS_INIT);

/*****************************************************************************/
/* Console commands */

#ifdef CONFIG_CMD_ALS
static int command_als(int argc, char **argv)
{
	int i, rv, val;

	for (i = 0; i < ALS_COUNT; i++) {
		ccprintf("%s: ", als[i].name);
		rv = als_read(i, &val);
		switch (rv) {
		case EC_SUCCESS:
			ccprintf("%d lux\n", val);
			break;
		default:
			ccprintf("Error %d\n", rv);
		}
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(als, command_als,
			NULL,
			"Print ALS values");
#endif
