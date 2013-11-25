/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Watchdog driver */

/*
 * TODO(crosbug.com/p/24107): Use independent timer for warning before watchdog
 *                            timer expires.
 */

#include "hooks.h"
#include "registers.h"
#include "watchdog.h"

void watchdog_reload(void)
{
	MEC1322_WDG_KICK = 1;
}
DECLARE_HOOK(HOOK_TICK, watchdog_reload, HOOK_PRIO_DEFAULT);

int watchdog_init(void)
{
	/* Set timeout. It takes 1007us to decrement WDG_CNT by 1. */
	MEC1322_WDG_LOAD = WATCHDOG_PERIOD_MS * 1000 / 1007;

	/* Start watchdog */
	MEC1322_WDG_CTL |= 1;

	return EC_SUCCESS;
}
