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
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"

int extpower_is_present(void)
{
	return gpio_get_level(GPIO_AC_PRESENT);
}

/**
 * Deferred function to handle external power change
 */
static void extpower_deferred(void)
{
	static int extpower_prev;
	int extpower = extpower_is_present();

	if (extpower && !extpower_prev) {
		charger_discharge_on_ac(0);
		/* If in G3, enable PP5000 for accurate sensing of CC */
		if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
			gpio_set_level(GPIO_PP5000_EN, 1);
	} else if (extpower && extpower_prev) {
		/* glitch on AC_PRESENT, attempt to recover from backboost */
		charger_discharge_on_ac(1);
		charger_discharge_on_ac(0);
	} else {
		charger_discharge_on_ac(1);
		/* If in G3, make sure PP5000 is off when no AC */
		if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
			gpio_set_level(GPIO_PP5000_EN, 0);
	}
	extpower_prev = extpower;

	hook_notify(HOOK_AC_CHANGE);

	/* Forward notification to host */
	if (extpower)
		host_set_single_event(EC_HOST_EVENT_AC_CONNECTED);
	else
		host_set_single_event(EC_HOST_EVENT_AC_DISCONNECTED);
}
DECLARE_DEFERRED(extpower_deferred);

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

	/* Trigger deferred notification of external power change */
	hook_call_deferred(extpower_deferred, 0);
}

static void extpower_init(void)
{
	extpower_buffer_to_pch();

	hook_call_deferred(extpower_deferred, 0);

	/* Enable interrupts, now that we've initialized */
	gpio_enable_interrupt(GPIO_AC_PRESENT);
}
DECLARE_HOOK(HOOK_INIT, extpower_init, HOOK_PRIO_DEFAULT);
