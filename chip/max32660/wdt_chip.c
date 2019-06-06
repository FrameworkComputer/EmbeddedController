/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MAX32660 Watchdog Module */

#include "clock.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "task.h"
#include "util.h"
#include "watchdog.h"
#include "console.h"
#include "registers.h"
#include "board.h"
#include "wdt_regs.h"

#define CPUTS(outstr) cputs(CC_COMMAND, outstr)
#define CPRINTS(format, args...) cprints(CC_COMMAND, format, ##args)

/* For a System clock of 96MHz, 
 *     Time in seconds = 96000000 / 2 * 2^power
 * Example for MXC_S_WDT_CTRL_INT_PERIOD_WDT2POW29
 *     Time in seconds = 96000000 / 2 * 2^29
 *                     = 11.1 Seconds
 */
#define WATCHDOG_TIMER_PERIOD MXC_S_WDT_CTRL_INT_PERIOD_WDT2POW29

volatile int starve_dog = 0;

void watchdog_reload(void)
{
	if (!starve_dog) {
		/* Reset the watchdog */
		MXC_WDT0->rst = 0x00A5;
		MXC_WDT0->rst = 0x005A;
	}
}
DECLARE_HOOK(HOOK_TICK, watchdog_reload, HOOK_PRIO_DEFAULT);

int watchdog_init(void)
{
	/* Set the Watchdog period */
	MXC_SETFIELD(MXC_WDT0->ctrl, MXC_F_WDT_CTRL_RST_PERIOD,
		     (WATCHDOG_TIMER_PERIOD << 4));

	/* We want the WD to reset us if it is not fed in time. */
	MXC_WDT0->ctrl |= MXC_F_WDT_CTRL_RST_EN;
	/* Enable the watchdog */
	MXC_WDT0->ctrl |= MXC_F_WDT_CTRL_WDT_EN;
	/* Reset the watchdog */
	MXC_WDT0->rst = 0x00A5;
	MXC_WDT0->rst = 0x005A;
	return EC_SUCCESS;
}

static int command_watchdog_test(int argc, char **argv)
{
	starve_dog = 1;

	CPRINTS("done command_watchdog_test.");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(wdttest, command_watchdog_test, "wdttest",
			"Force a WDT reset.");
