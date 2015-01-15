/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Pure GPIO-based external power detection, buffered to PCH.
 * Drive high in S5-S0 when AC_PRESENT is high, otherwise drive low.
 */

#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "task.h"
#include "util.h"

/* Backboost has been detected */
static int bkboost_detected;

int extpower_is_present(void)
{
	return gpio_get_level(GPIO_AC_PRESENT);
}

static void extpower_buffer_to_pch(void)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF)) {
		/* Drive low in G3 state */
		gpio_set_level(GPIO_PCH_ACOK, 0);
	} else {
		/* Buffer from extpower in S5+ (where 3.3DSW enabled) */
		gpio_set_level(GPIO_PCH_ACOK, extpower_is_present());
	}
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, extpower_buffer_to_pch, HOOK_PRIO_DEFAULT);

static void extpower_shutdown(void)
{
	/* Drive ACOK buffer to PCH low when shutting down */
	gpio_set_level(GPIO_PCH_ACOK, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, extpower_shutdown, HOOK_PRIO_DEFAULT);

void extpower_interrupt(enum gpio_signal signal)
{
	extpower_buffer_to_pch();

	/* Trigger notification of external power change */
	task_wake(TASK_ID_EXTPOWER);
}

static void extpower_init(void)
{
	extpower_buffer_to_pch();

	/* Enable interrupts, now that we've initialized */
	gpio_enable_interrupt(GPIO_AC_PRESENT);
}
DECLARE_HOOK(HOOK_INIT, extpower_init, HOOK_PRIO_DEFAULT);

static void extpower_board_hacks(int extpower)
{
	static int extpower_prev;

	/*
	 * Use discharge_on_ac() to workaround hardware backboosting
	 * charge circuit problems.
	 *
	 * When in G3, PP5000 needs to be enabled to accurately sense
	 * CC voltage when AC is attached. When AC is disconnceted
	 * it needs to be off to save power.
	 */
	if (extpower && !extpower_prev) {
		charger_discharge_on_ac(0);
		set_pp5000_in_g3(PP5000_IN_G3_AC, 1);
	} else if (extpower && extpower_prev) {
		/* Glitch on AC_PRESENT, attempt to recover from backboost */
		charger_discharge_on_ac(1);
		charger_discharge_on_ac(0);
	} else {
		charger_discharge_on_ac(1);
		set_pp5000_in_g3(PP5000_IN_G3_AC, 0);
	}
	extpower_prev = extpower;
}

/**
 * Task to handle external power change
 */
void extpower_task(void)
{
	int extpower = extpower_is_present();
	extpower_board_hacks(extpower);

	/* Enable backboost detection interrupt */
	gpio_enable_interrupt(GPIO_BKBOOST_DET);

	while (1) {
		/* Wait until next extpower interrupt */
		task_wait_event(-1);

		extpower = extpower_is_present();

		/* Various board hacks to run on extpower change */
		extpower_board_hacks(extpower);

		hook_notify(HOOK_AC_CHANGE);

		/* Forward notification to host */
		if (extpower)
			host_set_single_event(EC_HOST_EVENT_AC_CONNECTED);
		else
			host_set_single_event(EC_HOST_EVENT_AC_DISCONNECTED);
	}
}

void bkboost_det_interrupt(enum gpio_signal signal)
{
	/* Backboost has been detected, save it, and disable interrupt */
	bkboost_detected = 1;
	gpio_disable_interrupt(GPIO_BKBOOST_DET);
}

static int command_backboost_det(int argc, char **argv)
{
	ccprintf("Backboost detected: %d\n", bkboost_detected);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(bkboost, command_backboost_det, NULL,
			"Read backboost detection",
			NULL);
