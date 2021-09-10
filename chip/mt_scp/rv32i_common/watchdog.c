/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Watchdog driver */

#include "common.h"
#include "hooks.h"
#include "registers.h"
#include "scp_watchdog.h"
#include "watchdog.h"

void watchdog_reload(void)
{
	SCP_CORE_WDT_KICK = BIT(0);
}
DECLARE_HOOK(HOOK_TICK, watchdog_reload, HOOK_PRIO_DEFAULT);

void watchdog_disable(void)
{
	/* disable watchdog */
	SCP_CORE_WDT_CFG &= ~WDT_EN;
	/* clear watchdog irq */
	SCP_CORE_WDT_IRQ |= BIT(0);
}

void watchdog_enable(void)
{
	const uint32_t timeout = WDT_PERIOD(CONFIG_WATCHDOG_PERIOD_MS);

	/* disable watchdog */
	SCP_CORE_WDT_CFG &= ~WDT_EN;
	/* clear watchdog irq */
	SCP_CORE_WDT_IRQ |= BIT(0);
	/* enable watchdog */
	SCP_CORE_WDT_CFG = WDT_EN | timeout;
	/* reload watchdog */
	watchdog_reload();
}

int watchdog_init(void)
{
	watchdog_enable();

	return EC_SUCCESS;
}
