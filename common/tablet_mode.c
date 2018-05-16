/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_angle.h"
#include "tablet_mode.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_MOTION_LID, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_MOTION_LID, format, ## args)

/* 1: in tablet mode. 0: otherwise */
static int tablet_mode = 1;

int tablet_get_mode(void)
{
	return tablet_mode;
}

void tablet_set_mode(int mode)
{
	if (tablet_mode == mode)
		return;

	tablet_mode = mode;
	CPRINTS("tablet mode %sabled", mode ? "en" : "dis");
	hook_notify(HOOK_TABLET_MODE_CHANGE);
}

/* This ifdef can be removed once we clean up past projects which do own init */
#ifdef CONFIG_TABLET_SWITCH
#ifndef TABLET_MODE_GPIO_L
#error  TABLET_MODE_GPIO_L must be defined
#endif
static void tablet_mode_debounce(void)
{
	/* We won't reach here on boards without a dedicated tablet switch */
	tablet_set_mode(!gpio_get_level(TABLET_MODE_GPIO_L));

	/* Then, we disable peripherals only when the lid reaches 360 position.
	 * (It's probably already disabled by motion_sense_task.)
	 * We deliberately do not enable peripherals when the lid is leaving
	 * 360 position. Instead, we let motion_sense_task enable it once it
	 * reaches laptop zone (180 or less). */
	if (tablet_mode)
		lid_angle_peripheral_enable(0);
}
DECLARE_DEFERRED(tablet_mode_debounce);

#define TABLET_DEBOUNCE_US    (30 * MSEC)  /* Debounce time for tablet switch */

void tablet_mode_isr(enum gpio_signal signal)
{
	hook_call_deferred(&tablet_mode_debounce_data, TABLET_DEBOUNCE_US);
}

static void tablet_mode_init(void)
{
	gpio_enable_interrupt(TABLET_MODE_GPIO_L);
	/* Ensure tablet mode is initialized according to the hardware state
	 * so that the cached state reflects reality. */
	tablet_mode_debounce();
}
DECLARE_HOOK(HOOK_INIT, tablet_mode_init, HOOK_PRIO_DEFAULT);
#endif
