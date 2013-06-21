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

void watchdog_reload(void)
{
	/* Reload the watchdog */
	STM32_IWDG_KR = 0xaaaa;

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

#ifdef CONFIG_WATCHDOG_HELP
	/* Use a harder timer to warn about an impending watchdog reset */
	hwtimer_setup_watchdog();
#endif

	return EC_SUCCESS;
}
