/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Watchdog Timer
 *
 * In ISH, there is a watchdog timer available from the hardware. It is
 * controlled by a few registers:
 *
 * - WDT_CONTROL (consists of enable bit, T1, and T2 values): When T1
 *   reaches 0, a warning is fired. After T2 then reaches 0, the system
 *   will reset.
 * - WDT_RELOAD: Pet the watchdog by setting to 1
 * - WDT_VALUES: Gives software access to T1 and T2 if needed
 *
 * For ISH implementation, we wish to reset only the ISH. Waiting until
 * T2 expires will kill the whole system. The functionality of T2 is
 * ignored, and we simply call system_reset when T1 expires. T2 will
 * only be used if the system cannot reset when T1 expires.
 */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "task.h"
#include "registers.h"
#include "system.h"
#include "watchdog.h"

/* Units are hundreds of milliseconds */
#define WDT_T1_PERIOD		(100) /* 10 seconds */
#define WDT_T2_PERIOD		(10)  /* 1 second */

int watchdog_init(void)
{
	/* Initialize WDT clock divider */
	CCU_WDT_CD = WDT_CLOCK_HZ / 10; /* 10 Hz => 100 ms period */

	/* Enable the watchdog timer and set initial T1/T2 values */
	WDT_CONTROL = WDT_CONTROL_ENABLE_BIT
		| (WDT_T2_PERIOD << 8)
		| WDT_T1_PERIOD;

	task_enable_irq(ISH_WDT_IRQ);

	return EC_SUCCESS;
}

void watchdog_enable(void)
{
	WDT_CONTROL |= WDT_CONTROL_ENABLE_BIT;
}

void watchdog_disable(void)
{
	WDT_CONTROL &= ~WDT_CONTROL_ENABLE_BIT;
}

/* Parameters are pushed by hardware, we only care about %EIP */
__attribute__ ((noreturn))
void watchdog_warning(uint32_t errorcode,
		      uint32_t eip,
		      uint32_t cs,
		      uint32_t eflags)
{
	ccprintf("\nWDT Expired. EIP was 0x%08X. Resetting...\n", eip);
	cflush();

	system_reset(SYSTEM_RESET_AP_WATCHDOG);
	__builtin_unreachable();
}

__attribute__ ((noreturn))
void watchdog_warning_irq(void)
{
	/*
	 * Parameters to watchdog_warning were pushed by hardware, use
	 * asm here to re-use these parameters in the call.
	 */
	__asm__ ("call watchdog_warning\n");
	__builtin_unreachable();
}
DECLARE_IRQ(ISH_WDT_IRQ, watchdog_warning_irq);

void watchdog_reload(void)
{
	/*
	 * ISH Supplemental Registers Info, 1.2.6.2:
	 * "When firmware writes a 1 to this bit, hardware reloads
	 * the values in WDT_T1 and WDT_T2..."
	 */
	WDT_RELOAD = 1;
}
DECLARE_HOOK(HOOK_TICK, watchdog_reload, HOOK_PRIO_DEFAULT);
