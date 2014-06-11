/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* tiny substitute of the runtime layer */

#include "common.h"
#include "cpu.h"
#include "debug.h"
#include "irq_handler.h"
#include "master_slave.h"
#include "registers.h"
#include "timer.h"
#include "util.h"

volatile uint32_t last_event;

static uint32_t last_deadline;
static uint8_t need_wfi;

timestamp_t get_time(void)
{
	timestamp_t t;
	uint32_t hi, lo;

	do {
		hi = STM32_TIM_CNT(3);
		lo = STM32_TIM_CNT(2);
	} while (hi != STM32_TIM_CNT(3));

	t.le.lo = (hi << 16) | lo;
	t.le.hi = 0;

	return t;
}

void udelay(unsigned us)
{
	unsigned t0 = get_time().le.lo;
	while ((get_time().le.lo - t0) < us)
		;
}

void task_enable_irq(int irq)
{
	CPU_NVIC_EN(0) = 1 << irq;
}

void task_disable_irq(int irq)
{
	CPU_NVIC_DIS(0) = 1 << irq;
}

void task_clear_pending_irq(int irq)
{
	CPU_NVIC_UNPEND(0) = 1 << irq;
}

uint32_t task_set_event(task_id_t tskid, uint32_t event, int wait)
{
	last_event = event;

	return 0;
}

void IRQ_HANDLER(STM32_IRQ_TIM2)(void)
{
	if (STM32_TIM_CNT(3) == last_deadline >> 16) {
		STM32_TIM_DIER(2) = 0;
		task_clear_pending_irq(STM32_IRQ_TIM2);
		last_event = 1 << 29 /* task event wake */;
		need_wfi = 0;
	} else {
		need_wfi = 1;
	}
}

void __hw_clock_event_set(uint32_t deadline)
{
	last_deadline = deadline;
	STM32_TIM_CCR1(2) = deadline & 0xffff;
	STM32_TIM_SR(2) = ~2;
	STM32_TIM_DIER(2) |= 2;
}

uint32_t task_wait_event(int timeout_us)
{
	uint32_t evt;

	/* the event already happened */
	if (last_event || !timeout_us) {
		evt = last_event;
		last_event = 0;

		return evt;
	}

	/* set timeout on timer */
	if (timeout_us > 0)
		__hw_clock_event_set(get_time().le.lo + timeout_us);

	do {
		/* sleep until next interrupt */
		asm volatile("wfi");
	} while (need_wfi);
	STM32_TIM_DIER(2) = 0; /* disable match interrupt */
	evt = last_event;
	last_event = 0;

	return evt;
}

void system_reboot(void)
{
	if (master_slave_is_master()) {
		/* Ask the slave to reboot as well */
		STM32_GPIO_BSRR(GPIO_A) = 1 << (6 + 16);
		udelay(10 * MSEC); /* The slave reboots in 5 ms */
	}

	/* Ask the watchdog to trigger a hard reboot */
	STM32_IWDG_KR = 0x5555;
	STM32_IWDG_RLR = 0x1;
	STM32_IWDG_KR = 0xcccc;
	/* wait for the watchdog */
	while (1)
		;
}

/* --- stubs --- */
void __hw_timer_enable_clock(int n, int enable)
{ /* Done in hardware init */ }

void usleep(unsigned us)
{ /* Used only as a workaround */ }
