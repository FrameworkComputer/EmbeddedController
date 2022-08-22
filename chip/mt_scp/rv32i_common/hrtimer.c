/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * High-res hardware timer
 *
 * SCP hardware 32bit count down timer can be configured to source clock from
 * 32KHz, 26MHz, BCLK or PCLK. This implementation selects BCLK (ULPOSC1/8) as a
 * source, countdown mode and converts to micro second value matching common
 * timer.
 */

#include "common.h"
#include "hwtimer.h"
#include "registers.h"
#include "scp_timer.h"
#include "task.h"

#define TIMER_SYSTEM 5
#define TIMER_EVENT 3

#ifdef CHIP_VARIANT_MT8195
#define TIMER_CLOCK_MHZ 31
#else
#define TIMER_CLOCK_MHZ 32.5
#endif

#define OVERFLOW_TICKS (TIMER_CLOCK_MHZ * 0x100000000 - 1)

/* High 32-bit for system timer. */
static uint8_t sys_high;
/* High 32-bit for event timer. */
static uint8_t event_high;

void timer_enable(int n)
{
	/* cannot be changed when timer is enabled */
	SCP_CORE0_TIMER_IRQ_CTRL(n) |= TIMER_IRQ_EN;
	SCP_CORE0_TIMER_EN(n) |= TIMER_EN;
}

void timer_disable(int n)
{
	SCP_CORE0_TIMER_EN(n) &= ~TIMER_EN;
	/* cannot be changed when timer is enabled */
	SCP_CORE0_TIMER_IRQ_CTRL(n) &= ~TIMER_IRQ_EN;
}

uint32_t timer_read_raw_sr(void)
{
	return SCP_CORE0_TIMER_CUR_VAL(TIMER_SR);
}

static int timer_is_irq(int n)
{
	return SCP_CORE0_TIMER_IRQ_CTRL(n) & TIMER_IRQ_STATUS;
}

static void timer_ack_irq(int n)
{
	SCP_CORE0_TIMER_IRQ_CTRL(n) |= TIMER_IRQ_CLR;
}

static void timer_set_reset_value(int n, uint32_t reset_value)
{
	/* cannot be changed when timer is enabled */
	SCP_CORE0_TIMER_RST_VAL(n) = reset_value;
}

static void timer_set_clock(int n, uint32_t clock_source)
{
	SCP_CORE0_TIMER_EN(n) = (SCP_CORE0_TIMER_EN(n) & ~TIMER_CLK_SRC_MASK) |
				clock_source;
}

static void timer_reset(int n)
{
	timer_disable(n);
	timer_ack_irq(n);
	timer_set_reset_value(n, 0xffffffff);
	timer_set_clock(n, TIMER_CLK_SRC_32K);
}

/* Convert hardware countdown timer to 64bit countup ticks. */
static uint64_t timer_read_raw_system(void)
{
	uint32_t timer_ctrl = SCP_CORE0_TIMER_IRQ_CTRL(TIMER_SYSTEM);
	uint32_t sys_high_adj = sys_high;

	/*
	 * If an IRQ is pending, but has not been serviced yet, adjust the
	 * sys_high value.
	 */
	if (timer_ctrl & TIMER_IRQ_STATUS)
		sys_high_adj = sys_high ? (sys_high - 1) :
					  (TIMER_CLOCK_MHZ - 1);

	return OVERFLOW_TICKS - (((uint64_t)sys_high_adj << 32) |
				 SCP_CORE0_TIMER_CUR_VAL(TIMER_SYSTEM));
}

static uint64_t timer_read_raw_event(void)
{
	return OVERFLOW_TICKS - (((uint64_t)event_high << 32) |
				 SCP_CORE0_TIMER_CUR_VAL(TIMER_EVENT));
}

static void timer_reload(int n, uint32_t value)
{
	timer_disable(n);
	timer_set_reset_value(n, value);
	timer_enable(n);
}

static int timer_reload_event_high(void)
{
	if (event_high) {
		if (SCP_CORE0_TIMER_RST_VAL(TIMER_EVENT) == 0xffffffff)
			timer_enable(TIMER_EVENT);
		else
			timer_reload(TIMER_EVENT, 0xffffffff);
		event_high--;
		return 1;
	}

	timer_disable(TIMER_EVENT);
	return 0;
}

int __hw_clock_source_init(uint32_t start_t)
{
	int t;

	/* enable clock gate */
	SCP_SET_CLK_CG |= CG_TIMER_MCLK | CG_TIMER_BCLK;

	/* reset all timer, select 32768Hz clock source */
	for (t = 0; t < NUM_TIMERS; ++t)
		timer_reset(t);

	/* System timestamp timer */
	timer_set_clock(TIMER_SYSTEM, TIMER_CLK_SRC_BCLK);
	sys_high = TIMER_CLOCK_MHZ - 1;
	timer_set_reset_value(TIMER_SYSTEM, 0xffffffff);
	task_enable_irq(SCP_IRQ_TIMER(TIMER_SYSTEM));
	timer_enable(TIMER_SYSTEM);

	/* Event tick timer */
	timer_set_clock(TIMER_EVENT, TIMER_CLK_SRC_BCLK);
	task_enable_irq(SCP_IRQ_TIMER(TIMER_EVENT));

	/* SR timer */
	timer_set_clock(TIMER_SR, TIMER_CLK_SRC_26M);
	task_disable_irq(SCP_IRQ_TIMER(TIMER_SR));

	return SCP_IRQ_TIMER(TIMER_SYSTEM);
}

uint32_t __hw_clock_source_read(void)
{
	return timer_read_raw_system() / TIMER_CLOCK_MHZ;
}

uint32_t __hw_clock_event_get(void)
{
	return (timer_read_raw_event() + timer_read_raw_system()) /
	       TIMER_CLOCK_MHZ;
}

void __hw_clock_event_clear(void)
{
	/* c1ea4, magic number for clear state */
	timer_disable(TIMER_EVENT);
	timer_set_reset_value(TIMER_EVENT, 0x0000c1ea4);
	event_high = 0;
}

void __hw_clock_event_set(uint32_t deadline)
{
	uint64_t deadline_raw = (uint64_t)deadline * TIMER_CLOCK_MHZ;
	uint64_t now_raw = timer_read_raw_system();
	uint32_t event_deadline;

	if (deadline_raw > now_raw) {
		deadline_raw -= now_raw;
		event_deadline = (uint32_t)deadline_raw;
		event_high = deadline_raw >> 32;
	} else {
		event_deadline = 1;
		event_high = 0;
	}

	if (event_deadline)
		timer_reload(TIMER_EVENT, event_deadline);
	else
		timer_reload_event_high();
}

static void irq_group6_handler(void)
{
	extern volatile int ec_int;

	switch (ec_int) {
	case SCP_IRQ_TIMER(TIMER_EVENT):
		if (timer_is_irq(TIMER_EVENT)) {
			timer_ack_irq(TIMER_EVENT);

			if (!timer_reload_event_high())
				process_timers(0);

			task_clear_pending_irq(ec_int);
		}
		break;
	case SCP_IRQ_TIMER(TIMER_SYSTEM):
		/* If this is a hardware irq, check overflow */
		if (!in_soft_interrupt_context()) {
			timer_ack_irq(TIMER_SYSTEM);

			if (sys_high) {
				--sys_high;
				process_timers(0);
			} else {
				/* Overflow, reload system timer */
				sys_high = TIMER_CLOCK_MHZ - 1;
				process_timers(1);
			}

			task_clear_pending_irq(ec_int);
		} else {
			process_timers(0);
		}
		break;
	}
}
DECLARE_IRQ(6, irq_group6_handler, 0);
