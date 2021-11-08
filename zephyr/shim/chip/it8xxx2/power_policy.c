/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <pm/pm.h>
#include <pm/policy.h>
#include <soc.h>
#include <zephyr.h>

#include "system.h"
#include "timer.h"

static const struct pm_state_info pm_states[] =
	PM_STATE_INFO_LIST_FROM_DT_CPU(DT_NODELABEL(cpu0));

#define CONSOLE_IN_USE_ON_BOOT_TIME (5 * SECOND)
#define CONSOLE_IN_USE_TIMEOUT_SEC (5 * SECOND)

static timestamp_t console_expire_time;
static timestamp_t sleep_mode_t0;

void uart1_wui_isr_late(void)
{
	/* Set console in use expire time. */
	console_expire_time = get_time();
	console_expire_time.val += CONSOLE_IN_USE_TIMEOUT_SEC;
}

static int clock_allow_low_power_idle(void)
{
	sleep_mode_t0 = get_time();

	/* If we are waked up by console, then keep awake at least 5s. */
	if (sleep_mode_t0.val < console_expire_time.val)
		return 0;

	return 1;
}

/* CROS PM policy handler */
struct pm_state_info pm_policy_next_state(uint8_t cpu, int32_t ticks)
{
	ARG_UNUSED(cpu);

	/* Deep sleep is allowed and an interval of five seconds. */
	if (DEEP_SLEEP_ALLOWED && clock_allow_low_power_idle()) {
		/*
		 * If there are multiple power states, iterating backward
		 * is needed to take priority into account.
		 */
		for (int i = 0; i < ARRAY_SIZE(pm_states); i++) {
			/*
			 * To check if given power state is enabled and
			 * could be used.
			 */
			if (!pm_constraint_get(pm_states[i].state)) {
				continue;
			}

			return pm_states[i];
		}
	}

	return (struct pm_state_info){PM_STATE_ACTIVE, 0, 0};
}

static int power_policy_init(const struct device *arg)
{
	ARG_UNUSED(arg);

	console_expire_time.val = get_time().val + CONSOLE_IN_USE_ON_BOOT_TIME;

	return 0;
}
SYS_INIT(power_policy_init, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
