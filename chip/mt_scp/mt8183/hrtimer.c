/* Copyright 2018 The Chromium OS Authors. All rights reserved.
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

#include "clock.h"
#include "clock_chip.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "hwtimer.h"
#include "panic.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "watchdog.h"

#define IRQ_TIMER(n) CONCAT2(SCP_IRQ_TIMER, n)

#define TIMER_SYSTEM 5
#define TIMER_EVENT 3

/* ULPOSC1 should be a multiple of 8. */
BUILD_ASSERT((ULPOSC1_CLOCK_MHZ % 8) == 0);
#define TIMER_CLOCK_MHZ (ULPOSC1_CLOCK_MHZ / 8)

/* Common timer overflows at 0x100000000 micro seconds */
#define OVERFLOW_TICKS (TIMER_CLOCK_MHZ * 0x100000000 - 1)

static uint8_t sys_high;
static uint8_t event_high;

/* Convert hardware countdown timer to 64bit countup ticks */
static inline uint64_t timer_read_raw_system(void)
{
	uint32_t timer_ctrl = SCP_TIMER_IRQ_CTRL(TIMER_SYSTEM);
	uint32_t sys_high_adj = sys_high;

	/*
	 * If an IRQ is pending, but has not been serviced yet, adjust the
	 * sys_high value.
	 */
	if (timer_ctrl & TIMER_IRQ_STATUS)
		sys_high_adj = sys_high ? (sys_high - 1) : (TIMER_CLOCK_MHZ-1);

	return OVERFLOW_TICKS - (((uint64_t)sys_high_adj << 32) |
				 SCP_TIMER_VAL(TIMER_SYSTEM));
}

static inline uint64_t timer_read_raw_event(void)
{
	return OVERFLOW_TICKS - (((uint64_t)event_high << 32) |
				 SCP_TIMER_VAL(TIMER_EVENT));
}

static inline void timer_set_clock(int n, uint32_t clock_source)
{
	SCP_TIMER_EN(n) = (SCP_TIMER_EN(n) & ~TIMER_CLK_MASK) |
			  clock_source;
}

static inline void timer_ack_irq(int n)
{
	SCP_TIMER_IRQ_CTRL(n) |= TIMER_IRQ_CLEAR;
}

/* Set hardware countdown value */
static inline void timer_set_reset_value(int n, uint32_t reset_value)
{
	SCP_TIMER_RESET_VAL(n) = reset_value;
}

static void timer_reset(int n)
{
	__hw_timer_enable_clock(n, 0);
	timer_ack_irq(n);
	timer_set_reset_value(n, 0xffffffff);
	timer_set_clock(n, TIMER_CLK_32K);
}

/* Reload a new 32bit countdown value */
static void timer_reload(int n, uint32_t value)
{
	__hw_timer_enable_clock(n, 0);
	timer_set_reset_value(n, value);
	__hw_timer_enable_clock(n, 1);
}

static int timer_reload_event_high(void)
{
	if (event_high) {
		if (SCP_TIMER_RESET_VAL(TIMER_EVENT) == 0xffffffff)
			__hw_timer_enable_clock(TIMER_EVENT, 1);
		else
			timer_reload(TIMER_EVENT, 0xffffffff);
		event_high--;
		return 1;
	}

	/* Disable event timer clock when done. */
	__hw_timer_enable_clock(TIMER_EVENT, 0);
	return 0;
}

void __hw_clock_event_clear(void)
{
	__hw_timer_enable_clock(TIMER_EVENT, 0);
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

void __hw_timer_enable_clock(int n, int enable)
{
	if (enable) {
		SCP_TIMER_IRQ_CTRL(n) |= 1;
		SCP_TIMER_EN(n) |= 1;
	} else {
		SCP_TIMER_EN(n) &= ~1;
		SCP_TIMER_IRQ_CTRL(n) &= ~1;
	}
}

int __hw_clock_source_init(uint32_t start_t)
{
	int t;

	/*
	 * TODO(b/120169529): check clock tree to see if we need to turn on
	 * MCLK and BCLK gate.
	 */
	SCP_CLK_GATE |= (CG_TIMER_M | CG_TIMER_B);

	/* Reset all timer, select 32768Hz clock source */
	for (t = 0; t < NUM_TIMERS; t++)
		timer_reset(t);

	/* Enable timer IRQ wake source */
	SCP_INTC_IRQ_WAKEUP |= (1 << IRQ_TIMER(0)) | (1 << IRQ_TIMER(1)) |
			       (1 << IRQ_TIMER(2)) | (1 << IRQ_TIMER(3)) |
			       (1 << IRQ_TIMER(4)) | (1 << IRQ_TIMER(5));
	/*
	 * Timer configuration:
	 *   OS TIMER    - count up @ 13MHz, 64bit value with latch.
	 *   SYS TICK    - count down @ 26MHz
	 *   EVENT TICK  - count down @ 26MHz
	 */

	/* Turn on OS TIMER, tick at 13MHz */
	SCP_OSTIMER_CON |= 1;

	/* System timestamp timer from BCLK (sourced from ULPOSC) */
	SCP_CLK_BCLK = CLK_BCLK_SEL_ULPOSC1_DIV8;

	timer_set_clock(TIMER_SYSTEM, TIMER_CLK_BCLK);
	sys_high = TIMER_CLOCK_MHZ-1;
	timer_set_reset_value(TIMER_SYSTEM, 0xffffffff);
	__hw_timer_enable_clock(TIMER_SYSTEM, 1);
	task_enable_irq(IRQ_TIMER(TIMER_SYSTEM));
	/* Event tick timer */
	timer_set_clock(TIMER_EVENT, TIMER_CLK_BCLK);
	task_enable_irq(IRQ_TIMER(TIMER_EVENT));

	return IRQ_TIMER(TIMER_SYSTEM);
}

uint32_t __hw_clock_source_read(void)
{
	return timer_read_raw_system() / TIMER_CLOCK_MHZ;
}

uint32_t __hw_clock_event_get(void)
{
	return (timer_read_raw_event() + timer_read_raw_system())
			/ TIMER_CLOCK_MHZ;
}

static void __hw_clock_source_irq(int n)
{
	uint32_t timer_ctrl = SCP_TIMER_IRQ_CTRL(n);

	/* Ack if we're hardware interrupt */
	if (timer_ctrl & TIMER_IRQ_STATUS)
		timer_ack_irq(n);

	switch (n) {
	case TIMER_EVENT:
		if (timer_ctrl & TIMER_IRQ_STATUS) {
			if (timer_reload_event_high())
				return;
		}
		process_timers(0);
		break;
	case TIMER_SYSTEM:
		/* If this is a hardware irq, check overflow */
		if (timer_ctrl & TIMER_IRQ_STATUS) {
			if (sys_high) {
				sys_high--;
				process_timers(0);
			} else {
				/* Overflow, reload system timer */
				sys_high = TIMER_CLOCK_MHZ-1;
				process_timers(1);
			}
		} else {
			process_timers(0);
		}
		break;
	default:
		return;
	}

}

#define DECLARE_TIMER_IRQ(n) \
	void __hw_clock_source_irq_##n(void) { __hw_clock_source_irq(n); } \
	DECLARE_IRQ(IRQ_TIMER(n), __hw_clock_source_irq_##n, 2)

DECLARE_TIMER_IRQ(0);
DECLARE_TIMER_IRQ(1);
DECLARE_TIMER_IRQ(2);
DECLARE_TIMER_IRQ(3);
DECLARE_TIMER_IRQ(4);
DECLARE_TIMER_IRQ(5);
