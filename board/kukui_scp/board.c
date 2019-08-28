/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Kukui SCP configuration */

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "power.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Build GPIO tables */
void eint_event(enum gpio_signal signal);

#include "gpio_list.h"


void eint_event(enum gpio_signal signal)
{
	ccprintf("EINT event: %d\n", signal);
}

/* Initialize board.  */
static void board_init(void)
{
	gpio_enable_interrupt(GPIO_EINT5_TP);
	gpio_enable_interrupt(GPIO_EINT6_TP);
	gpio_enable_interrupt(GPIO_EINT7_TP);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

__override void
power_chipset_handle_host_sleep_event(enum host_sleep_event state,
				      struct host_sleep_event_context *ctx)
{
	int i;
	const task_id_t s3_suspend_tasks[] = {
#ifndef S3_SUSPEND_TASK_LIST
#define S3_SUSPEND_TASK_LIST
#endif
#define TASK(n, ...) TASK_ID_##n,
		S3_SUSPEND_TASK_LIST
	};

	if (state == HOST_SLEEP_EVENT_S3_SUSPEND) {
		ccprints("AP suspend");
		for (i = 0; i < ARRAY_SIZE(s3_suspend_tasks); ++i)
			task_disable_task(s3_suspend_tasks[i]);
	} else if (state == HOST_SLEEP_EVENT_S3_RESUME) {
		ccprints("AP resume");
		for (i = 0; i < ARRAY_SIZE(s3_suspend_tasks); ++i)
			task_enable_task(s3_suspend_tasks[i]);
	}
}
