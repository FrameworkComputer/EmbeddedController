/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "hooks.h"
#include "hwtimer.h"
#include "init_chip.h"
#include "registers.h"
#include "task.h"
#include "util.h"

/* The frequency of timerls is 256k so there are about 4usec/tick */
#define USEC_PER_TICK 4
/*
 * Scale the maximum number of ticks so that it will only count up to the
 * equivalent of 0xffffffff usecs.
 */
#define TIMELS_MAX (0xffffffff / USEC_PER_TICK)

#define SOURCE(field) TIMER0_##field
#define EVENT(field) TIMER1_##field

static inline uint32_t ticks_to_usecs(uint32_t ticks)
{
	return ticks * USEC_PER_TICK;
}

static inline uint32_t usec_to_ticks(uint32_t next_evt_us)
{
	return next_evt_us / USEC_PER_TICK;
}

uint32_t __hw_clock_event_get(void)
{
	/* At what time will the next event fire? */
	return __hw_clock_source_read() +
		ticks_to_usecs(GREG32(TIMELS, EVENT(VALUE)));
}

void __hw_clock_event_clear(void)
{
	/* one-shot, 32-bit, timer & interrupts disabled, 1:1 prescale */
	GWRITE_FIELD(TIMELS, EVENT(CONTROL), ENABLE, 0);

	/* Disable interrupts */
	GWRITE(TIMELS, EVENT(IER), 0);

	/* Clear any pending interrupts */
	GWRITE(TIMELS, EVENT(WAKEUP_ACK), 1);
	GWRITE(TIMELS, EVENT(IAR), 1);
}

void __hw_clock_event_set(uint32_t deadline)
{
	uint32_t event_time;

	__hw_clock_event_clear();

	/* How long from the current time to the deadline? */
	event_time = (deadline - __hw_clock_source_read());

	/* Convert event_time to ticks rounding up */
	GREG32(TIMELS, EVENT(LOAD)) =
		((uint64_t)(event_time + USEC_PER_TICK - 1) / USEC_PER_TICK);

	/* Enable the timer & interrupts */
	GWRITE(TIMELS, EVENT(IER), 1);
	GWRITE_FIELD(TIMELS, EVENT(CONTROL), ENABLE, 1);
}

/*
 * Handle event matches. It's priority matches the HW rollover irq to prevent
 * a race condition that could lead to a watchdog timeout if preempted after
 * the get_time() call in process_timers().
 */
void __hw_clock_event_irq(void)
{
	__hw_clock_event_clear();
	process_timers(0);
}
DECLARE_IRQ(GC_IRQNUM_TIMELS0_TIMINT1, __hw_clock_event_irq, 1);

uint32_t __hw_clock_source_read(void)
{
	/*
	 * Return the current time in usecs. Since the counter counts down,
	 * we have to invert the value.
	 */
	return ticks_to_usecs(TIMELS_MAX - GREG32(TIMELS, SOURCE(VALUE)));
}

void __hw_clock_source_set(uint32_t ts)
{
	GREG32(TIMELS, SOURCE(LOAD)) = (0xffffffff - ts) / USEC_PER_TICK;
}

/* This handles rollover in the HW timer */
void __hw_clock_source_irq(void)
{
	/* Clear the interrupt */
	GWRITE(TIMELS, SOURCE(WAKEUP_ACK), 1);
	GWRITE(TIMELS, SOURCE(IAR), 1);

	/* Reset the load value */
	GREG32(TIMELS, SOURCE(LOAD)) = TIMELS_MAX;

	process_timers(1);
}
DECLARE_IRQ(GC_IRQNUM_TIMELS0_TIMINT0, __hw_clock_source_irq, 1);

int __hw_clock_source_init(uint32_t start_t)
{

	if (runlevel_is_high()) {
		/* Verify the contents of CC_TRIM are valid */
		ASSERT(GR_FUSE(RC_RTC_OSC256K_CC_EN) == 0x5);

		/* Initialize RTC to 256kHz */
		GWRITE_FIELD(RTC, CTRL, X_RTC_RC_CTRL,
			GR_FUSE(RC_RTC_OSC256K_CC_TRIM));
	}

	/* Configure timer1 */
	GREG32(TIMELS, EVENT(LOAD)) = TIMELS_MAX;
	GREG32(TIMELS, EVENT(RELOADVAL)) = TIMELS_MAX;
	GWRITE_FIELD(TIMELS, EVENT(CONTROL), WRAP, 1);
	GWRITE_FIELD(TIMELS, EVENT(CONTROL), RELOAD, 0);
	GWRITE_FIELD(TIMELS, EVENT(CONTROL), ENABLE, 0);

	/* Configure timer0 */
	GREG32(TIMELS, SOURCE(RELOADVAL)) = TIMELS_MAX;
	GWRITE_FIELD(TIMELS, SOURCE(CONTROL), WRAP, 1);
	GWRITE_FIELD(TIMELS, SOURCE(CONTROL), RELOAD, 1);

	/* Event timer disabled */
	__hw_clock_event_clear();

	/* Clear any pending interrupts */
	GWRITE(TIMELS, SOURCE(WAKEUP_ACK), 1);

	/* Force the time to whatever we're told it is */
	__hw_clock_source_set(start_t);

	/* HW Timer enabled, periodic, interrupt enabled, 32-bit, wrapping */
	GWRITE_FIELD(TIMELS, SOURCE(CONTROL), ENABLE, 1);

	/* Enable source timer interrupts */
	GWRITE(TIMELS, SOURCE(IER), 1);

	/* Here we go... */
	task_enable_irq(GC_IRQNUM_TIMELS0_TIMINT0);
	task_enable_irq(GC_IRQNUM_TIMELS0_TIMINT1);

	/* Return the Event timer IRQ number (NOT the HW timer IRQ) */
	return GC_IRQNUM_TIMELS0_TIMINT1;
}
