/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hardware timers driver for ISH High Precision Event Timers (HPET) */

#include "console.h"
#include "hpet.h"
#include "hwtimer.h"
#include "timer.h"
#include "registers.h"
#include "task.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_CLOCK, outstr)
#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_CLOCK, format, ## args)

static uint32_t last_deadline;

/*
 * The ISH hardware needs at least 25 ticks of leeway to arms the timer.
 * ISH4/5 are the slowest with 32kHz timers, so we wait at least 800us when
 * scheduling events in the future
 */
#define MINIMUM_EVENT_DELAY_US 800

/*
 * ISH HPET timer HW has latency for interrupt, on ISH5, this latency is about
 * 3 ticks, defined this configuration to calibrate the 'last_deadline' which is
 * updated in event timer interrupt ISR. Without this calibration, we could
 * get negative sleep time in idle task for low power sleep process.
 */
#define HPET_INT_LATENCY_TICKS 3

/* Scaling helper methods for different ISH chip variants */
#ifdef CHIP_FAMILY_ISH3
#define CLOCK_FACTOR 12
BUILD_ASSERT(CLOCK_FACTOR * SECOND == ISH_HPET_CLK_FREQ);

static inline uint64_t scale_us2ticks(uint64_t us)
{
	return us * CLOCK_FACTOR;
}

static inline uint32_t scale_us2ticks_32(uint32_t us)
{
	/* no optimization for ISH3 */
	return us * CLOCK_FACTOR;
}

static inline uint64_t scale_ticks2us(uint64_t ticks)
{
	return ticks / CLOCK_FACTOR;
}

static inline void wait_while_settling(uint32_t mask)
{
	/* Do nothing on ISH3, only ISH4 and ISH5 need settling */
}

#elif defined(CHIP_FAMILY_ISH4) || defined(CHIP_FAMILY_ISH5)
#define CLOCK_SCALE_BITS 15
BUILD_ASSERT(BIT(CLOCK_SCALE_BITS) == ISH_HPET_CLK_FREQ);

/* Slow version, for 64-bit precision */
static inline uint64_t scale_us2ticks(uint64_t us)
{
	/* ticks = us * ISH_HPET_CLK_FREQ / SECOND */

	return (us << CLOCK_SCALE_BITS) / SECOND;
}

/* Fast version, for 32-bit precision */
static inline uint32_t scale_us2ticks_32(uint32_t us)
{
	/*
	 * GCC optimizes this shift/divide into multiplication by a
	 * magic number
	 */
	return (us << CLOCK_SCALE_BITS) / SECOND;
}

static inline uint64_t scale_ticks2us(uint64_t ticks)
{
	return (ticks * SECOND) >> CLOCK_SCALE_BITS;
}

/*
 * HPET Control & Status register may indicate that a value which has
 * been written still needs propogated by hardware. Before updating
 * HPET_TIMER_CONF_CAP(N), be sure to wait on the value settling with
 * the corresponding mask (see hpet.h).
 */
static inline void wait_while_settling(uint32_t mask)
{
	/* Wait for timer settings to settle ~ 150us */
	while (HPET_CTRL_STATUS & mask)
		continue;
}

#else
#error "Must define CHIP_FAMILY_ISH(3|4|5)"
#endif

/*
 * The 64-bit read on a 32-bit chip can tear during the read. Ensure that the
 * value returned for 64-bit didn't rollover while we were reading it.
 */
static inline uint64_t read_main_timer(void)
{
	timestamp_t t;
	uint32_t hi;

	/* need check main counter if valid when exit low power TCG mode */
	wait_while_settling(HPET_MAIN_COUNTER_VALID);

	do {
		t.le.hi = HPET_MAIN_COUNTER_64_HI;
		t.le.lo = HPET_MAIN_COUNTER_64_LO;
		hi = HPET_MAIN_COUNTER_64_HI;
	} while (t.le.hi != hi);

	return t.val;
}

void __hw_clock_event_set(uint32_t deadline)
{
	uint32_t remaining_us;
	uint32_t current_us;
	uint64_t current_ticks;

	/* 'current_ticks' is the current absolute 64bit HW timer counter */
	current_ticks = read_main_timer();

	/*
	 * 'current_us' is the low 32bit part of current time in 64bit micro
	 * seconds format, it's can express 2^32 micro seconds in maximum.
	 */
	current_us = scale_ticks2us(current_ticks);

	/*
	 * To ensure HW has enough time to react to the new timer value,
	 * we make remaining time not less than 'MINIMUM_EVENT_DELAY_US'
	 */
	remaining_us = deadline - current_us;
	remaining_us = MAX(remaining_us, MINIMUM_EVENT_DELAY_US);

	/*
	 * Set new 64bit absolute timeout ticks to Timer 1 comparator
	 * register.
	 * For ISH3, this assumes that remaining_us is less than 360 seconds
	 * (2^32 us / 12Mhz), otherwise we would need to handle 32-bit rollover
	 * of 12Mhz timer comparator value. Watchdog refresh happens at least
	 * every 10 seconds.
	 */
	wait_while_settling(HPET_T1_CMP_SETTLING);
	HPET_TIMER_COMP(1) = current_ticks + scale_us2ticks_32(remaining_us);

	/*
	 * Update 'last_deadline' and add calibrate delta due to HPET timer
	 * interrupt latency.
	 */
	last_deadline = current_us + remaining_us;
	last_deadline += scale_ticks2us(HPET_INT_LATENCY_TICKS);

	/* Enable timer interrupt */
	wait_while_settling(HPET_T1_SETTLING);
	HPET_TIMER_CONF_CAP(1) |= HPET_Tn_INT_ENB_CNF;
}

uint32_t __hw_clock_event_get(void)
{
	return last_deadline;
}

void __hw_clock_event_clear(void)
{
	/*
	 * We need to make sure that process_timers is called when the
	 * event timer rolls over, set for deadline when
	 * process_timers clears the event timer.
	 */
	__hw_clock_event_set(0xFFFFFFFF);
}

uint64_t __hw_clock_source_read64(void)
{
	return scale_ticks2us(read_main_timer());
}

void __hw_clock_source_set64(uint64_t timestamp)
{
	/* Reset both clock and overflow comparators */
	wait_while_settling(HPET_ANY_SETTLING);
	HPET_GENERAL_CONFIG &= ~HPET_ENABLE_CNF;

	HPET_MAIN_COUNTER_64 = scale_us2ticks(timestamp);

	wait_while_settling(HPET_ANY_SETTLING);
	HPET_GENERAL_CONFIG |= HPET_ENABLE_CNF;
}

static void hw_clock_event_isr(void)
{
	/* Clear interrupt */
	wait_while_settling(HPET_INT_STATUS_SETTLING);
	HPET_INTR_CLEAR = BIT(1);

	process_timers(0);
}
DECLARE_IRQ(ISH_HPET_TIMER1_IRQ, hw_clock_event_isr);

int __hw_clock_source_init64(uint64_t start_t)
{
	/*
	 * Timer 1 is used as an event timer. Timer 0 is unused, as
	 * CONFIG_HWTIMER_64BIT is enabled.
	 */
	uint32_t timer1_config = 0x00000000;

	/* Disable HPET */
	wait_while_settling(HPET_ANY_SETTLING);
	HPET_GENERAL_CONFIG &= ~HPET_ENABLE_CNF;

	/* Disable T0 */
	HPET_TIMER_CONF_CAP(0) &= ~HPET_Tn_INT_ENB_CNF;

	/* Disable T1 until we get it set up (below) */
	HPET_TIMER_CONF_CAP(1) &= ~HPET_Tn_INT_ENB_CNF;

	/* Initialize main counter */
	HPET_MAIN_COUNTER_64 = scale_us2ticks(start_t);

	/* Clear any interrupts from previously running image */
	HPET_INTR_CLEAR = BIT(0);
	HPET_INTR_CLEAR = BIT(1);

	/* Timer 1 - IRQ routing */
	timer1_config &= ~HPET_Tn_INT_ROUTE_CNF_MASK;
	timer1_config |= (ISH_HPET_TIMER1_IRQ <<
			  HPET_Tn_INT_ROUTE_CNF_SHIFT);

	/* Level triggered interrupt */
	timer1_config |= HPET_Tn_INT_TYPE_CNF;

	/* Initialize last_deadline until an event is scheduled */
	last_deadline = 0xFFFFFFFF;

	/* Before enabling, previous values must have settled */
	wait_while_settling(HPET_ANY_SETTLING);

	/* Unmask HPET IRQ in IOAPIC */
	task_enable_irq(ISH_HPET_TIMER1_IRQ);

	/* Copy timer config to hardware register */
	HPET_TIMER_CONF_CAP(1) |= timer1_config;

	/* Enable HPET */
	HPET_GENERAL_CONFIG |= (HPET_ENABLE_CNF | HPET_LEGACY_RT_CNF);

	/* Return IRQ value for OS event timer */
	return ISH_HPET_TIMER1_IRQ;
}
