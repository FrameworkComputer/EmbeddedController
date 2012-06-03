/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Watchdog driver */

#include "board.h"
#include "common.h"
#include "config.h"
#include "registers.h"
#include "gpio.h"
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
}


/**
 * Chcek if a watchdog interrupt needs to be reported.
 *
 * If so, this function should call watchdog_trace()
 *
 * @param excep_lr	Value of lr to indicate caller return
 * @param excep_sp	Value of sp to indicate caller task id
 */
void watchdog_check(uint32_t excep_lr, uint32_t excep_sp)
{
	/* Reset the windowed watchdog here */
	STM32_WWDG_CR = 0xff;
	STM32_WWDG_SR = 0;

	/* If the count has expired, output a trace */
	if (!--watchdog_count) {
		/* Reset here, to give the UART enough time to send trace */
		watchdog_reset_count();
		watchdog_trace(excep_lr, excep_sp);
		watchdog_reset_count();
	}
}


void IRQ_HANDLER(STM32_IRQ_WWDG)(void) __attribute__((naked));
void IRQ_HANDLER(STM32_IRQ_WWDG)(void)
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
const struct irq_priority IRQ_BUILD_NAME(prio_, STM32_IRQ_WWDG, )
	__attribute__((section(".rodata.irqprio")))
		= {STM32_IRQ_WWDG, 0}; /* put the watchdog at the highest
					    priority */

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
	/* enable clock */
	STM32_RCC_APB1ENR |= 1 << 11;

	STM32_WWDG_SR = 0;
	STM32_WWDG_CR = 0xff;
	STM32_WWDG_CFR = 0x7f | STM32_WWDG_TB_8 | STM32_WWDG_EWI;

	/* Enable watchdog interrupt */
	task_enable_irq(IRQ_WATCHDOG);
#endif

	return EC_SUCCESS;
}
