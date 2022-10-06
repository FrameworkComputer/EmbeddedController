/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Chipset module for emulator */

/* Does not run a chipset task, but does emulate an AP chipset */
#define CONFIG_AP_POWER_CONTROL

#include <stdio.h>
#include "chipset.h"
#include "common.h"
#include "hooks.h"
#include "task.h"
#include "test_util.h"

static int chipset_state = CHIPSET_STATE_SOFT_OFF;
static int power_on_req;
static int power_off_req;

test_mockable void chipset_reset(enum chipset_shutdown_reason reason)
{
	fprintf(stderr, "Chipset reset: %d!\n", reason);
}

test_mockable void chipset_throttle_cpu(int throttle)
{
	/* Do nothing */
}

test_mockable void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	/* Do nothing */
}

test_mockable int chipset_in_state(int state_mask)
{
	return state_mask & chipset_state;
}

test_mockable int chipset_in_or_transitioning_to_state(int state_mask)
{
	return state_mask & chipset_state;
}

void test_chipset_on(void)
{
	if (chipset_in_state(CHIPSET_STATE_ON))
		return;
	power_on_req = 1;
	task_wake(TASK_ID_CHIPSET);
}

void test_chipset_off(void)
{
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		return;
	power_off_req = 1;
	task_wake(TASK_ID_CHIPSET);
}

test_mockable void chipset_task(void)
{
	while (1) {
		while (!power_on_req)
			task_wait_event(-1);
		power_on_req = 0;
		hook_notify(HOOK_CHIPSET_PRE_INIT);
		chipset_state = CHIPSET_STATE_ON;
		hook_notify(HOOK_CHIPSET_STARTUP);
		while (!power_off_req)
			task_wait_event(-1);
		power_off_req = 0;
		chipset_state = CHIPSET_STATE_SOFT_OFF;
		hook_notify(HOOK_CHIPSET_SHUTDOWN);
		hook_notify(HOOK_CHIPSET_SHUTDOWN_COMPLETE);
	}
}
