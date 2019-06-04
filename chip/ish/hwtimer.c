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
 * Number of ticks for timer0 comparator representing 2^32 us seconds. SECONDS
 * represents the expected clock frequency of the OS (i.e. 1 Mhz).
 */
#define ROLLOVER_CMP_VAL (((uint64_t)ISH_HPET_CLK_FREQ << 32) / SECOND)

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

static inline uint64_t scale_us2ticks(uint32_t us)
{
	return (uint64_t)us * CLOCK_FACTOR;
}

static inline uint32_t scale_ticks2us(uint64_t ticks)
{
	/*
	 * We drop into asm here since this is on the critical path of reading
	 * the hardware timer for clock result. This needs to be efficient.
	 *
	 * Modulating hi first ensures that the quotient fits in 32-bits due to
	 * the follow math:
	 * Let ticks = (hi << 32) + lo;
	 * Let hi = N*CLOCK_FACTOR + R; where R is hi % CLOCK_FACTOR
	 *
	 * ticks = (N*CLOCK_FACTOR << 32) + (R << 32) + lo
	 *
	 * ticks / CLOCK_FACTOR = ((N*CLOCK_FACTOR << 32) + (R << 32) + lo) /
	 *                        CLOCK_FACTOR
	 * ticks / CLOCK_FACTOR = (N*CLOCK_FACTOR << 32) / CLOCK_FACTOR +
	 *                        (R << 32) / CLOCK_FACTOR +
	 *                        lo / CLOCK_FACTOR
	 * ticks / CLOCK_FACTOR = (N << 32) +
	 *                        (R << 32) / CLOCK_FACTOR +
	 *                        lo / CLOCK_FACTOR
	 * If we want to truncate to 32 bits, then the N << 32 can be dropped.
	 * (ticks / CLOCK_FACTOR) & 0xFFFFFFFF = ((R << 32) + lo) / CLOCK_FACTOR
	 */
	const uint32_t divisor = CLOCK_FACTOR;
	const uint32_t hi = ((uint32_t)(ticks >> 32)) % divisor;
	const uint32_t lo = ticks;
	uint32_t quotient;

	asm("divl %3" : "=a"(quotient) : "d"(hi), "a"(lo), "rm"(divisor));
	return quotient;
}

static inline void wait_while_settling(uint32_t mask)
{
	/* Do nothing on ISH3, only ISH4 and ISH5 need settling */
	(void) mask;
}

#elif defined(CHIP_FAMILY_ISH4) || defined(CHIP_FAMILY_ISH5)
#define CLOCK_SCALE_BITS 15
BUILD_ASSERT(BIT(CLOCK_SCALE_BITS) == ISH_HPET_CLK_FREQ);

static inline uint32_t scale_us2ticks(uint32_t us)
{
	/*
	 * ticks = us * ISH_HPET_CLK_FREQ / SECOND;
	 *
	 * First multiple us by ISH_HPET_CLK_FREQ via bit shift, then use
	 * 64-bit div into 32-bit result.
	 *
	 * We use asm directly to maintain full 32-bit precision without using
	 * an iterative divide (i.e. 64-bit / 64-bit => 64-bit). We use the
	 * 64-bit / 32-bit => 32-bit asm instruction directly since there is no
	 * way to emitted that instruction via the compiler.
	 *
	 * The intermediate result of (us * ISH_HPET_CLK_FREQ) needs 64-bits of
	 * precision to maintain full 32-bit precision for the end result.
	 */
	const uint32_t hi = us >> (32 - CLOCK_SCALE_BITS);
	const uint32_t lo = us << CLOCK_SCALE_BITS;
	const uint32_t divisor = SECOND;
	uint32_t ticks;

	asm("divl %3" : "=a"(ticks) : "d"(hi), "a"(lo), "rm"(divisor));
	return ticks;
}

static inline uint32_t scale_ticks2us(uint64_t ticks)
{
	/*
	 * us = ticks / ISH_HPET_CLK_FREQ * SECOND;
	 */
	const uint64_t intermediate = (uint64_t)ticks * SECOND;

	return intermediate >> CLOCK_SCALE_BITS;
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
	HPET_TIMER_COMP(1) = current_ticks + scale_us2ticks(remaining_us);

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
	 * we get timer event at every new clksrc_high.
	 * so when there's no event, last_dealine should be
	 * the last value within current clksrc_high.
	 */
	last_deadline = 0xFFFFFFFF;
	wait_while_settling(HPET_T1_SETTLING);
	HPET_TIMER_CONF_CAP(1) &= ~HPET_Tn_INT_ENB_CNF;
}

uint32_t __hw_clock_source_read(void)
{
	return scale_ticks2us(read_main_timer());
}

void __hw_clock_source_set(uint32_t ts)
{
	/* Reset both clock and overflow comparators */
	wait_while_settling(HPET_ANY_SETTLING);
	HPET_GENERAL_CONFIG &= ~HPET_ENABLE_CNF;

	HPET_MAIN_COUNTER_64 = scale_us2ticks(ts);
	HPET_TIMER0_COMP_64 = ROLLOVER_CMP_VAL;

	wait_while_settling(HPET_ANY_SETTLING);
	HPET_GENERAL_CONFIG |= HPET_ENABLE_CNF;
}

static void __hw_clock_source_irq(int timer_id)
{
	/* Clear interrupt */
	wait_while_settling(HPET_INT_STATUS_SETTLING);
	HPET_INTR_CLEAR = BIT(timer_id);

	/*
	 * If IRQ is from timer 0, 2^32 us have elapsed (i.e. OS timer
	 * overflowed).
	 */
	process_timers(timer_id == 0);
}

void __hw_clock_source_irq_0(void)
{
	__hw_clock_source_irq(0);
}
DECLARE_IRQ(ISH_HPET_TIMER0_IRQ, __hw_clock_source_irq_0);

void __hw_clock_source_irq_1(void)
{
	__hw_clock_source_irq(1);
}
DECLARE_IRQ(ISH_HPET_TIMER1_IRQ, __hw_clock_source_irq_1);

int __hw_clock_source_init(uint32_t start_t)
{
	/*
	 * The timer can only fire interrupt when its value reaches zero.
	 * Therefore we need two timers:
	 *   - Timer 0 as free running timer
	 *   - Timer 1 as event timer
	 */

	uint32_t timer0_config = 0x00000000;
	uint32_t timer1_config = 0x00000000;

	/* Disable HPET */
	wait_while_settling(HPET_ANY_SETTLING);
	HPET_GENERAL_CONFIG &= ~HPET_ENABLE_CNF;

	/* Disable T0 and T1 until we get them set up (below) */
	HPET_TIMER_CONF_CAP(0) &= ~HPET_Tn_INT_ENB_CNF;
	HPET_TIMER_CONF_CAP(1) &= ~HPET_Tn_INT_ENB_CNF;

	/* Initialize main counter */
	HPET_MAIN_COUNTER_64 = scale_us2ticks(start_t);

	/* Clear any interrupts from previously running image */
	HPET_INTR_CLEAR = BIT(0);
	HPET_INTR_CLEAR = BIT(1);

	/* Set comparator value for Timer 0 and enable periodic mode */
	HPET_TIMER0_COMP_64 = ROLLOVER_CMP_VAL;
	timer0_config |= HPET_Tn_TYPE_CNF;

	/* Timer 0 - IRQ routing, no need IRQ set for HPET0 */
	timer0_config &= ~HPET_Tn_INT_ROUTE_CNF_MASK;

	/* Timer 1 - IRQ routing */
	timer1_config &= ~HPET_Tn_INT_ROUTE_CNF_MASK;
	timer1_config |= (ISH_HPET_TIMER1_IRQ <<
			  HPET_Tn_INT_ROUTE_CNF_SHIFT);

	/* Level triggered interrupt */
	timer0_config |= HPET_Tn_INT_TYPE_CNF;
	timer1_config |= HPET_Tn_INT_TYPE_CNF;

	/* no event until next timer 0 IRQ for clksrc_high++ */
	last_deadline = 0xFFFFFFFF;

	/* Enable interrupt */
	timer0_config |= HPET_Tn_INT_ENB_CNF;

	/* Before enabling, previous values must have settled */
	wait_while_settling(HPET_ANY_SETTLING);

	/* Unmask HPET IRQ in IOAPIC */
	task_enable_irq(ISH_HPET_TIMER0_IRQ);
	task_enable_irq(ISH_HPET_TIMER1_IRQ);

	/* Set timer 0/1 config */
	HPET_TIMER_CONF_CAP(0) |= timer0_config;
	HPET_TIMER_CONF_CAP(1) |= timer1_config;

	/* Enable HPET */
	HPET_GENERAL_CONFIG |= HPET_ENABLE_CNF;

	/* Return IRQ value for OS event timer */
	return ISH_HPET_TIMER1_IRQ;
}
