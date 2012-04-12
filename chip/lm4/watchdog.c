/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Watchdog driver */

#include "board.h"
#include "clock.h"
#include "common.h"
#include "config.h"
#include "registers.h"
#include "gpio.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

/*
 * We use watchdog 0 which is clocked on the system clock
 * to avoid the penalty cycles on each write access
 */

/* magic value to unlock the watchdog registers */
#define LM4_WATCHDOG_MAGIC_WORD  0x1ACCE551

#define WATCHDOG_PERIOD_MS 1100  /* Watchdog period in ms */
#define WATCHDOG_RELOAD_MS 500   /* Interval in ms between reloads of the
				  * watchdog timer.  Should be less than half
				  * of the watchdog period. */

static uint32_t watchdog_period;     /* Watchdog counter initial value */

/* console debug command prototypes */
int command_task_info(int argc, char **argv);
int command_timer_info(int argc, char **argv);

/**
 * watchdog debug trace.
 *
 * It is triggered if the watchdog has not been reloaded after 1x the timeout
 * period, after 2x the period an hardware reset is triggering.
 */
void watchdog_trace(uint32_t excep_lr, uint32_t excep_sp)
{
	uint32_t psp;
	uint32_t *stack;

	/* we do NOT reset the watchdog interrupt here, it will be done in
	 * watchdog_reload() or fire the reset
	 * instead de-activate the interrupt in the NVIC :
	 * so, we will get the trace only once
	 */
	task_disable_irq(LM4_IRQ_WATCHDOG);

	asm("mrs %0, psp":"=r"(psp));
	if ((excep_lr & 0xf) == 1) {
		/* we were already in exception context */
		stack = (uint32_t *)excep_sp;
	} else {
		/* we were in task context */
		stack = (uint32_t *)psp;
	}

	uart_printf("### WATCHDOG PC=%08x / LR=%08x / pSP=%08x ###\n",
	            stack[6], stack[5], psp);
	/* ensure this debug message is always flushed to the UART */
	uart_emergency_flush();
	/* if we are blocked in a high priority IT handler, the following
	 * debug messages might not appear but they are useless in that
	 * situation.
	 */
	command_task_info(0, NULL);
	uart_emergency_flush();
	command_timer_info(0, NULL);
	uart_emergency_flush();
}

void IRQ_HANDLER(LM4_IRQ_WATCHDOG)(void) __attribute__((naked));
void IRQ_HANDLER(LM4_IRQ_WATCHDOG)(void)
{
	asm volatile("mov r0, lr\n"
	             "mov r1, sp\n"
	             "push {lr}\n"
	             "bl watchdog_trace\n"
	             "pop {lr}\n"
	             "mov r0, lr\n"
		     "b task_resched_if_needed\n");
}
const struct irq_priority IRQ_BUILD_NAME(prio_, LM4_IRQ_WATCHDOG, )
	__attribute__((section(".rodata.irqprio")))
		= {LM4_IRQ_WATCHDOG, 0}; /* put the watchdog at the highest
					    priority */

void watchdog_reload(void)
{
	uint32_t status = LM4_WATCHDOG_RIS(0);

	/* Unlock watchdog registers */
	LM4_WATCHDOG_LOCK(0) = LM4_WATCHDOG_MAGIC_WORD;

	/* As we reboot only on the second timeout, if we have already reached
	 * the first timeout we need to reset the interrupt bit. */
	if (status) {
		LM4_WATCHDOG_ICR(0) = status;
		/* That doesn't seem to unpend the watchdog interrupt (even if
		 * we do dummy writes to force the write to be committed), so
		 * explicitly unpend the interrupt before re-enabling it. */
		task_clear_pending_irq(LM4_IRQ_WATCHDOG);
		task_enable_irq(LM4_IRQ_WATCHDOG);
	}

	/* Reload the watchdog counter */
	LM4_WATCHDOG_LOAD(0) = watchdog_period;

	/* Re-lock watchdog registers */
	LM4_WATCHDOG_LOCK(0) = 0xdeaddead;
}


void watchdog_clock_changed(int freq)
{
	/* Set the timeout period */
	watchdog_period = WATCHDOG_PERIOD_MS * (freq / 1000);
}


int watchdog_init(void)
{
	volatile uint32_t scratch  __attribute__((unused));

	/* Enable watchdog 0 clock */
	LM4_SYSTEM_RCGCWD |= 0x1;
	/* Wait 3 clock cycles before using the module */
	scratch = LM4_SYSTEM_RCGCWD;

	/* Unlock watchdog registers */
	LM4_WATCHDOG_LOCK(0) = LM4_WATCHDOG_MAGIC_WORD;

	/* Set initial timeout period */
	watchdog_clock_changed(clock_get_freq());
	LM4_WATCHDOG_LOAD(0) = watchdog_period;

	/* De-activate the watchdog when the JTAG stops the CPU */
	LM4_WATCHDOG_TEST(0) |= 1 << 8;

	/* Reset after 2 time-out, activate the watchdog and lock the control
	 * register. */
	LM4_WATCHDOG_CTL(0) = 0x3;

	/* Reset watchdog interrupt bits */
	LM4_WATCHDOG_ICR(0) = LM4_WATCHDOG_RIS(0);

	/* Lock watchdog registers against unintended accesses */
	LM4_WATCHDOG_LOCK(0) = 0xdeaddead;

	/* Enable watchdog interrupt */
	task_enable_irq(LM4_IRQ_WATCHDOG);

	return EC_SUCCESS;
}


/* Low priority task to reload the watchdog */
void watchdog_task(void)
{
	/* Print when the watchdog task starts.  This is the lowest priority
	 * task, so this only starts once all other tasks have gotten a chance
	 * to do their task inits and have gone to sleep. */
	uart_printf("[watchdog task started at %d us]\n", get_time().le.lo);

	while (1) {
#ifdef BOARD_bds
		gpio_set_level(GPIO_DEBUG_LED, 1);
#endif
		usleep(WATCHDOG_RELOAD_MS * 1000);
		watchdog_reload();
#ifdef BOARD_bds
		gpio_set_level(GPIO_DEBUG_LED, 0);
#endif
		usleep(WATCHDOG_RELOAD_MS * 1000);
		watchdog_reload();
	}
}
