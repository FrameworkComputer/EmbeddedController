/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "hooks.h"
#include "hwtimer.h"
#include "init_chip.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define SOURCE(field) TIMER0_##field
#define EVENT(field) TIMER1_##field

/* The frequency of timerls is 8 * 32768 Hz. */
#define TIMER_FREQ_HZ (8 * 32768)

/*
 * GCD(SECOND, TIMER_FREQ_HZ) = 64. We'll need to use reduced terms to prevent
 * overflow of our intermediate uint32_t type in some calculations.
 */
#define GCD 64
#define TIMER_FREQ_GCD (TIMER_FREQ_HZ / GCD)
#define TIME_GCD (SECOND / GCD)

/*
 * Scale the maximum number of ticks so that it will only count up to the
 * equivalent of approximately 0xffffffff usecs. Note that we lose 3us on
 * timer wrap due to loss of precision during division.
 */
#define TIMELS_MAX (usecs_to_ticks(0xffffffff))

/*
 * The below calculation is lightweight and can be implemented using
 * umull + shift on 32-bit ARM.
 */
static inline uint32_t ticks_to_usecs(uint32_t ticks)
{
	return (uint64_t)ticks * SECOND / TIMER_FREQ_HZ;
}

/*
 * The below calulation is more tricky, this is very inefficient and requires
 * 64-bit division:
 * return ((uint64_t)(usecs) * TIMER_FREQ_HZ / SECOND);
 * Instead use 32 bit vals, divide first, and add back the loss of precision.
 */
static inline uint32_t usecs_to_ticks(uint32_t usecs)
{
	return (usecs / TIME_GCD * TIMER_FREQ_GCD) +
	       ((usecs % TIME_GCD) * TIMER_FREQ_GCD / TIME_GCD);
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
	GREG32(TIMELS, EVENT(LOAD)) = usecs_to_ticks(event_time) + 1;

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
	GREG32(TIMELS, SOURCE(LOAD)) = usecs_to_ticks(0xffffffff - ts);
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

#ifdef CONFIG_HW_SPECIFIC_UDELAY
/*
 * Custom chip/g udelay(), guaranteed to delay for at least us microseconds.
 *
 * Lost time during timer wrap is not taken into account since interrupt latency
 * and __hw_clock_source_irq() execution time likely exceeds the lost 3us.
 */
void udelay(unsigned us)
{
	unsigned t0 = __hw_clock_source_read();

	/*
	 * The timer will tick either 3 us or 4 us, every ~3.8us in realtime.
	 * To ensure we meet the minimum delay, we must wait out a full
	 * longest-case timer tick (4 us), since a tick may have occurred
	 * immediately after sampling t0.
	 */
	us += ticks_to_usecs(1) + 1;

	/*
	 * udelay() may be called with interrupts disabled, so we can't rely on
	 * process_timers() updating the top 32 bits.  So handle wraparound
	 * ourselves rather than calling get_time() and comparing with a
	 * deadline.
	 *
	 * This may fail for delays close to 2^32 us (~4000 sec), because the
	 * subtraction below can overflow.  That's acceptable, because the
	 * watchdog timer would have tripped long before that anyway.
	 */
	while (__hw_clock_source_read() - t0 <= us)
		;
}
#endif /* CONFIG_HW_SPECIFIC_UDELAY */
