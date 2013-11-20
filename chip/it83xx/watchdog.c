/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Watchdog driver */

#include "common.h"
#include "cpu.h"
#include "hooks.h"
#include "panic.h"
#include "registers.h"
#include "task.h"
#include "watchdog.h"

/*
 * We use timer3 to trigger an interrupt just before the watchdog timer
 * will fire so that we can capture important state information before
 * being reset.
 */

/* Magic value to tickle the watchdog register. */
#define ITE83XX_WATCHDOG_MAGIC_WORD  0x5C

void watchdog_warning_irq(void)
{
	/* clear interrupt status */
	task_clear_pending_irq(IT83XX_IRQ_EXT_TIMER3);

	/* Reset warning timer (timer 3). */
	IT83XX_ETWD_ET3CTRL = 0x03;

	panic_printf("Pre-watchdog warning! IPC: %08x\n", get_ipc());
}

void watchdog_reload(void)
{
	/* Reset warning timer (timer 3). */
	IT83XX_ETWD_ET3CTRL = 0x03;

	/* Restart (tickle) watchdog timer. */
	IT83XX_ETWD_EWDKEYR = ITE83XX_WATCHDOG_MAGIC_WORD;
}
DECLARE_HOOK(HOOK_TICK, watchdog_reload, HOOK_PRIO_DEFAULT);

int watchdog_init(void)
{
	/* Unlock access to watchdog registers. */
	IT83XX_ETWD_ETWCFG = 0x00;

	/* Set timer 3 and WD timer to use 1.024kHz clock. */
	IT83XX_ETWD_ET3PSR = 0x01;
	IT83XX_ETWD_ET1PSR = 0x01;

	/* Set WDT key match enabled and WDT clock to use ET1PSR. */
	IT83XX_ETWD_ETWCFG = 0x30;

	/* Specify that watchdog cannot be stopped. */
	IT83XX_ETWD_ETWCTRL = 0x00;

	/* Set timer 3 load value to 1024 (~1.05 seconds). */
	IT83XX_ETWD_ET3CNTLH2R = 0x00;
	IT83XX_ETWD_ET3CNTLHR = 0x04;
	IT83XX_ETWD_ET3CNTLLR = 0x00;

	/* Enable interrupt on timer 3 expiration. */
	task_enable_irq(IT83XX_IRQ_EXT_TIMER3);

	/* Start timer 3. */
	IT83XX_ETWD_ET3CTRL = 0x03;

	/* Start timer 1 (must be started for watchdog timer to run). */
	IT83XX_ETWD_ET1CNTLLR = 0x00;

	/* Set watchdog timer to ~1.3 seconds. Writing CNTLL starts timer. */
	IT83XX_ETWD_EWDCNTLHR = 0x05;
	IT83XX_ETWD_EWDCNTLLR = 0x00;

	/* Lock access to watchdog registers. */
	IT83XX_ETWD_ETWCFG = 0x3f;

	return EC_SUCCESS;
}
