/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Watchdog driver */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "registers.h"
#include "hwtimer_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "timer.h"
#include "task.h"
#include "util.h"
#include "system_chip.h"
#include "watchdog.h"

/* WDCNT value for watchdog period */
#define WDCNT_VALUE \
	((CONFIG_WATCHDOG_PERIOD_MS * INT_32K_CLOCK) / (1024 * 1000))

void watchdog_init_warning_timer(void)
{
	/* init watchdog timer first */
	init_hw_timer(ITIM_WDG_NO, ITIM_SOURCE_CLOCK_32K);

	/*
	 * prescaler to TIMER_TICK
	 * Ttick_unit = (PRE_8+1) * T32k
	 * PRE_8 = (Ttick_unit/T32K) - 1
	 * Unit: 1 msec
	 */
	NPCX_ITPRE(ITIM_WDG_NO) =
		DIV_ROUND_NEAREST(1000 * INT_32K_CLOCK, SECOND) - 1;

	/* Event module disable */
	CLEAR_BIT(NPCX_ITCTS(ITIM_WDG_NO), NPCX_ITCTS_ITEN);
	/* ITIM count down : event expired */
	NPCX_ITCNT(ITIM_WDG_NO) = CONFIG_AUX_TIMER_PERIOD_MS;
	/* Event module enable */
	SET_BIT(NPCX_ITCTS(ITIM_WDG_NO), NPCX_ITCTS_ITEN);
	/* Enable interrupt of ITIM */
	task_enable_irq(ITIM_INT(ITIM_WDG_NO));
}

static timestamp_t last_watchdog_touch;
void watchdog_stop_and_unlock(void)
{
	/*
	 * Ensure we have waited at least 3 watchdog ticks since touching WD
	 * timer. 3 / (32768 / 1024) HZ = 93.75ms
	 */
	while (time_since32(last_watchdog_touch) < (100 * MSEC))
		continue;

	NPCX_WDSDM = 0x87;
	NPCX_WDSDM = 0x61;
	NPCX_WDSDM = 0x63;
}

static void touch_watchdog_count(void)
{
	NPCX_WDSDM = 0x5C;
	last_watchdog_touch = get_time();
}

static void watchdog_reload_warning_timer(void)
{
	/* Disable warning timer module  */
	CLEAR_BIT(NPCX_ITCTS(ITIM_WDG_NO), NPCX_ITCTS_ITEN);
	/* Wait for module disable to take effect before updating count */
	while (IS_BIT_SET(NPCX_ITCTS(ITIM_WDG_NO), NPCX_ITCTS_ITEN))
		;

	/* Reload the warning timer count */
	NPCX_ITCNT(ITIM_WDG_NO) = CONFIG_AUX_TIMER_PERIOD_MS;

	/* enable warning timer module  */
	SET_BIT(NPCX_ITCTS(ITIM_WDG_NO), NPCX_ITCTS_ITEN);
	/* Wait for module enable */
	while (!IS_BIT_SET(NPCX_ITCTS(ITIM_WDG_NO), NPCX_ITCTS_ITEN))
		;
}

void __keep watchdog_check(uint32_t excep_lr, uint32_t excep_sp)
{
#ifdef CONFIG_TASK_PROFILING
	/*
	 * Perform IRQ profiling accounting. This is normally done by
	 * DECLARE_IRQ(), but we are not using that for ITIM_WDG_NO.
	 */
	task_start_irq_handler((void *)excep_lr);
#endif

	/* Clear timeout status for event */
	SET_BIT(NPCX_ITCTS(ITIM_WDG_NO), NPCX_ITCTS_TO_STS);

	/* Print panic info */
	watchdog_trace(excep_lr, excep_sp);
}

/* ISR for watchdog warning naked will keep SP & LR */
void IRQ_HANDLER(ITIM_INT(ITIM_WDG_NO))(void) __attribute__((naked));
void IRQ_HANDLER(ITIM_INT(ITIM_WDG_NO))(void)
{
	/* Naked call so we can extract raw LR and SP */
	asm volatile("mov r0, lr\n"
			"mov r1, sp\n"
			/* Must push registers in pairs to keep 64-bit aligned
			 * stack for ARM EABI.  This also conveninently saves
			 * R0=LR so we can pass it to task_resched_if_needed. */
			"push {r0, lr}\n"
			"bl watchdog_check\n"
			"pop {r0, lr}\n"
			"b task_resched_if_needed\n");
}
const struct irq_priority __keep IRQ_PRIORITY(ITIM_INT(ITIM_WDG_NO))
__attribute__((section(".rodata.irqprio")))
= {ITIM_INT(ITIM_WDG_NO), 0};
/* put the watchdog at the highest priority */

void watchdog_reload(void)
{
	/* Disable watchdog interrupt */
	task_disable_irq(ITIM_INT(ITIM_WDG_NO));

	watchdog_reload_warning_timer();

#if 1 /* mark this for testing watchdog */
	/* Touch watchdog & reset software counter */
	touch_watchdog_count();
#endif

	/* Enable watchdog interrupt */
	task_enable_irq(ITIM_INT(ITIM_WDG_NO));
}
DECLARE_HOOK(HOOK_TICK, watchdog_reload, HOOK_PRIO_DEFAULT);

int watchdog_init(void)
{
#if SUPPORT_WDG
	/* Touch watchdog before init if it is already running */
	if (IS_BIT_SET(NPCX_T0CSR, NPCX_T0CSR_WD_RUN))
		touch_watchdog_count();

	/* Keep prescaler ratio timer0 clock to 1:1024 */
	NPCX_TWCP = 0x0A;
	/* Keep prescaler ratio watchdog clock to 1:1 */
	NPCX_WDCP = 0;

	/* Clear watchdog reset status initially*/
	SET_BIT(NPCX_T0CSR, NPCX_T0CSR_WDRST_STS);

	/* Reset TWCFG */
	NPCX_TWCFG = 0;
	/* Watchdog touch by writing 5Ch to WDSDM */
	SET_BIT(NPCX_TWCFG, NPCX_TWCFG_WDSDME);
	/* Select T0IN clock as watchdog prescaler clock */
	SET_BIT(NPCX_TWCFG, NPCX_TWCFG_WDCT0I);
	/* Disable early touch functionality */
	SET_BIT(NPCX_T0CSR, NPCX_T0CSR_TESDIS);

	/*
	 * Set WDCNT initial reload value and T0OUT timeout period
	 * WDCNT = 0 will generate watchdog reset
	 */
	NPCX_WDCNT = WDCNT_VALUE;

	/* Disable interrupt */
	interrupt_disable();
	/* Reload TWDT0/WDCNT */
	SET_BIT(NPCX_T0CSR, NPCX_T0CSR_RST);
	/* Wait for timer is loaded and restart */
	while (IS_BIT_SET(NPCX_T0CSR, NPCX_T0CSR_RST))
		;
	/* Enable interrupt */
	interrupt_enable();

	/* Init watchdog warning timer */
	watchdog_init_warning_timer();
#endif
	return EC_SUCCESS;
}
