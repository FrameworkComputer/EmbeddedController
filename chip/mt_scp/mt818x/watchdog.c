/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Watchdog driver */

#include "common.h"
#include "hooks.h"
#include "panic.h"
#include "registers.h"
#include "watchdog.h"

void watchdog_reload(void)
{
	SCP_WDT_RELOAD = SCP_WDT_RELOAD_VALUE;
}
DECLARE_HOOK(HOOK_TICK, watchdog_reload, HOOK_PRIO_DEFAULT);

int watchdog_init(void)
{
	const uint32_t watchdog_timeout =
		SCP_WDT_PERIOD(CONFIG_WATCHDOG_PERIOD_MS);

	/* Disable watchdog */
	SCP_WDT_CFG = 0;
	/* Enable watchdog */
	SCP_WDT_CFG = SCP_WDT_ENABLE | watchdog_timeout;
	/* Reload watchdog */
	watchdog_reload();

	return EC_SUCCESS;
}
