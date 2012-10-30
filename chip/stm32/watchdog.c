/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Watchdog driver */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "hwtimer.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

/* LSI oscillator frequency is typically 38 kHz
 * but might vary from 28 to 56kHz.
 * So let's pick 56kHz to ensure we reload
 * early enough.
 */
#define LSI_CLOCK 56000

/* Prescaler divider = /256 */
#define IWDG_PRESCALER 6
#define IWDG_PRESCALER_DIV (1 << ((IWDG_PRESCALER) + 2))

/*
 * We use the WWDG as an early warning for the real watchdog, which just
 * resets. Since it has a very short period, we need to allow several cycles
 * of this to make up one IWDG cycle. The WWDG's early warning kicks in
 * half way through the cycle, with a maximum time of 65.54ms at 32 MHz.
 */
#define WATCHDOG_CYCLES_BEFORE_RESET \
	(WATCHDOG_PERIOD_MS / (65540 * 32000 / CPU_CLOCK))

/* Keep a track of how many WWDG cycles we have had */
static unsigned int watchdog_count;


static void watchdog_reset_count(void)
{
	watchdog_count = WATCHDOG_CYCLES_BEFORE_RESET;
}


void watchdog_reload(void)
{
	/* Reload the watchdog */
	STM32_IWDG_KR = 0xaaaa;

	watchdog_reset_count();
#ifdef CONFIG_WATCHDOG_HELP
	hwtimer_reset_watchdog();
#endif
}
DECLARE_HOOK(HOOK_TICK, watchdog_reload, HOOK_PRIO_DEFAULT);

int watchdog_init(void)
{
	uint32_t watchdog_period;

	/* set the time-out period */
	watchdog_period = WATCHDOG_PERIOD_MS *
			(LSI_CLOCK / IWDG_PRESCALER_DIV) / 1000;

	/* Unlock watchdog registers */
	STM32_IWDG_KR = 0x5555;

	/* Set the prescaler between the LSI clock and the watchdog counter */
	STM32_IWDG_PR = IWDG_PRESCALER & 7;
	/* Set the reload value of the watchdog counter */
	STM32_IWDG_RLR = watchdog_period & 0x7FF ;

	/* Start the watchdog (and re-lock registers) */
	STM32_IWDG_KR = 0xcccc;

	watchdog_reset_count();

#ifdef CONFIG_WATCHDOG_HELP
	/* Use a harder timer to warn about an impending watchdog reset */
	hwtimer_setup_watchdog();
#endif

	return EC_SUCCESS;
}
