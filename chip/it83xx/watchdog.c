/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Watchdog driver */

#include "common.h"
#include "cpu.h"
#include "hooks.h"
#include "hwtimer_chip.h"
#include "panic.h"
#include "registers.h"
#include "task.h"
#include "watchdog.h"

/* Panic data goes at the end of RAM. */
static struct panic_data * const pdata_ptr = PANIC_DATA_PTR;

/*
 * We use WDT_EXT_TIMER to trigger an interrupt just before the watchdog timer
 * will fire so that we can capture important state information before
 * being reset.
 */

/* Magic value to tickle the watchdog register. */
#define ITE83XX_WATCHDOG_MAGIC_WORD  0x5C

void watchdog_warning_irq(void)
{
#ifdef CONFIG_SOFTWARE_PANIC
	pdata_ptr->nds_n8.ipc = get_ipc();
#endif
	/* clear interrupt status */
	task_clear_pending_irq(et_ctrl_regs[WDT_EXT_TIMER].irq);

	/* Reset warning timer. */
	IT83XX_ETWD_ETXCTRL(WDT_EXT_TIMER) = 0x03;

	panic_printf("Pre-watchdog warning! IPC: %08x\n", get_ipc());
}

void watchdog_reload(void)
{
	/* Reset warning timer. */
	IT83XX_ETWD_ETXCTRL(WDT_EXT_TIMER) = 0x03;

	/* Restart (tickle) watchdog timer. */
	IT83XX_ETWD_EWDKEYR = ITE83XX_WATCHDOG_MAGIC_WORD;
}
DECLARE_HOOK(HOOK_TICK, watchdog_reload, HOOK_PRIO_DEFAULT);

int watchdog_init(void)
{
	uint16_t wdt_count = CONFIG_WATCHDOG_PERIOD_MS * 1024 / 1000;

	/* Unlock access to watchdog registers. */
	IT83XX_ETWD_ETWCFG = 0x00;

	/* Set WD timer to use 1.024kHz clock. */
	IT83XX_ETWD_ET1PSR = 0x01;

	/* Set WDT key match enabled and WDT clock to use ET1PSR. */
	IT83XX_ETWD_ETWCFG = 0x30;

#ifdef CONFIG_HIBERNATE
	/* bit4: watchdog can be stopped. */
	IT83XX_ETWD_ETWCTRL |= (1 << 4);
#else
	/* Specify that watchdog cannot be stopped. */
	IT83XX_ETWD_ETWCTRL = 0x00;
#endif

	/* Start WDT_EXT_TIMER (CONFIG_AUX_TIMER_PERIOD_MS ms). */
	ext_timer_ms(WDT_EXT_TIMER, EXT_PSR_32P768K_HZ, 1, 1,
			CONFIG_AUX_TIMER_PERIOD_MS, 1, 0);

	/* Start timer 1 (must be started for watchdog timer to run). */
	IT83XX_ETWD_ET1CNTLLR = 0x00;

	/*
	 * Set watchdog timer to CONFIG_WATCHDOG_PERIOD_MS ms.
	 * Writing CNTLL starts timer.
	 */
	IT83XX_ETWD_EWDCNTLHR = (wdt_count >> 8) & 0xff;
	IT83XX_ETWD_EWDCNTLLR = wdt_count & 0xff;

	/* Lock access to watchdog registers. */
	IT83XX_ETWD_ETWCFG = 0x3f;

	return EC_SUCCESS;
}
