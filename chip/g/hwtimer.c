/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "hooks.h"
#include "hwtimer.h"
#include "registers.h"
#include "task.h"
#include "util.h"

/*
 * Other chips can interrupt at arbitrary match points. We can only interrupt
 * at zero, so we'll have to use a separate timer for events. We'll use module
 * 0, timer 1 for the current time and module 0, timer 2 for event timers.
 *
 * Oh, and we can't control the rate at which the timers tick. We expect to
 * have all counter values in microseconds, but instead they'll be some factor
 * faster than that. 1 usec / tick == 1 MHz, so if PCLK is 30 MHz, we'll have
 * to divide the hardware counter by 30 to get the values expected outside of
 * this file.
 */
static uint32_t clock_mul_factor;
static uint32_t clock_div_factor;
static uint32_t hw_rollover_count;

static inline uint32_t ticks_to_usecs(uint32_t ticks)
{
	return hw_rollover_count * clock_div_factor + ticks / clock_mul_factor;
}

static inline uint32_t usecs_to_ticks(uint32_t usecs)
{
	/* Really large counts will just be scheduled more than once */
	if (usecs >= clock_div_factor)
		return 0xffffffff;

	return usecs * clock_mul_factor;
}

static void update_prescaler(void)
{
	/*
	 * We want the timer to tick every microsecond, but we can only divide
	 * PCLK by 1, 16, or 256. We're targeting 30MHz, so we'll just let it
	 * run at 1:1.
	 */
	REG_WRITE_MLV(GR_TIMEHS_CONTROL(0, 1),
		      GC_TIMEHS_TIMER1CONTROL_PRE_MASK,
		      GC_TIMEHS_TIMER1CONTROL_PRE_LSB, 0);
	REG_WRITE_MLV(GR_TIMEHS_CONTROL(0, 2),
		      GC_TIMEHS_TIMER1CONTROL_PRE_MASK,
		      GC_TIMEHS_TIMER1CONTROL_PRE_LSB, 0);

	/*
	 * We're not yet doing anything to detect the current frequency, we're
	 * just hard-coding it. We're also assuming the clock rate is an
	 * integer multiple of MHz.
	 */
	clock_mul_factor = 26;			/* NOTE: prototype board */
	clock_div_factor = 0xffffffff / clock_mul_factor;
}
DECLARE_HOOK(HOOK_FREQ_CHANGE, update_prescaler, HOOK_PRIO_DEFAULT);

uint32_t __hw_clock_event_get(void)
{
	/* At what time will the next event fire? */
	uint32_t time_now_in_ticks;
	time_now_in_ticks = (0xffffffff - GR_TIMEHS_VALUE(0, 1));
	return ticks_to_usecs(time_now_in_ticks + GR_TIMEHS_VALUE(0, 2));
}

void __hw_clock_event_clear(void)
{
	/* one-shot, 32-bit, timer & interrupts disabled, 1:1 prescale */
	GR_TIMEHS_CONTROL(0, 2) = 0x3;
	/* Clear any pending interrupts */
	GR_TIMEHS_INTCLR(0, 2) = 0x1;
}

void __hw_clock_event_set(uint32_t deadline)
{
	uint32_t time_now_in_ticks;

	__hw_clock_event_clear();

	/* How long from the current time to the deadline? */
	time_now_in_ticks = (0xffffffff - GR_TIMEHS_VALUE(0, 1));
	GR_TIMEHS_LOAD(0, 2) = usecs_to_ticks(deadline) - time_now_in_ticks;

	/* timer & interrupts enabled */
	GR_TIMEHS_CONTROL(0, 2) = 0xa3;
}

/*
 * Handle event matches. It's lower priority than the HW rollover irq, so it
 * will always be either before or after a rollover exception.
 */
void __hw_clock_event_irq(void)
{
	__hw_clock_event_clear();
	process_timers(0);
}
DECLARE_IRQ(GC_IRQNUM_TIMEHS0_TIMINT2, __hw_clock_event_irq, 2);

uint32_t __hw_clock_source_read(void)
{
	/*
	 * Return the current time in usecs. Since the counter counts down,
	 * we have to invert the value.
	 */
	return ticks_to_usecs(0xffffffff - GR_TIMEHS_VALUE(0, 1));
}

void __hw_clock_source_set(uint32_t ts)
{
	GR_TIMEHS_LOAD(0, 1) = 0xffffffff - usecs_to_ticks(ts);
}

/* This handles rollover in the HW timer */
void __hw_clock_source_irq(void)
{
	/* Clear the interrupt */
	GR_TIMEHS_INTCLR(0, 1) = 0x1;

	/* The one-tick-per-clock HW counter has rolled over. */
	hw_rollover_count++;
	/* Has the system's usec counter rolled over? */
	if (hw_rollover_count >= clock_mul_factor) {
		hw_rollover_count = 0;
		process_timers(1);
	} else {
		process_timers(0);
	}
}
DECLARE_IRQ(GC_IRQNUM_TIMEHS0_TIMINT1, __hw_clock_source_irq, 1);

int __hw_clock_source_init(uint32_t start_t)
{
	/* Set the reload and current value. */
	GR_TIMEHS_BGLOAD(0, 1) = 0xffffffff;
	GR_TIMEHS_LOAD(0, 1) = 0xffffffff;

	/* HW Timer enabled, periodic, interrupt enabled, 32-bit, wrapping */
	GR_TIMEHS_CONTROL(0, 1) = 0xe2;
	/* Event timer disabled */
	__hw_clock_event_clear();

	/* Account for the clock speed. */
	update_prescaler();

	/* Clear any pending interrupts */
	GR_TIMEHS_INTCLR(0, 1) = 0x1;

	/* Force the time to whatever we're told it is */
	__hw_clock_source_set(start_t);

	/* Here we go... */
	task_enable_irq(GC_IRQNUM_TIMEHS0_TIMINT1);
	task_enable_irq(GC_IRQNUM_TIMEHS0_TIMINT2);

	/* Return the Event timer IRQ number (NOT the HW timer IRQ) */
	return GC_IRQNUM_TIMEHS0_TIMINT2;
}
