/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hardware timer driver for Rotor MCU */

#include "clock.h"
#include "common.h"
#include "hwtimer.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/*
 * Timer 0 is the clock timer.
 * Timer 1 is the event timer.
 */

#ifdef BOARD_REI
/* static uint32_t reload_val; */
#else
static uint32_t rollover_cnt;
#endif /* defined(BOARD_REI) */


void __hw_clock_event_set(uint32_t deadline)
{
	uint32_t ticks;
	uint32_t delta;

	__hw_clock_event_clear();

	delta = deadline - __hw_clock_source_read();

	/* Convert the delta to ticks. */
	ticks = delta * (clock_get_freq() / SECOND);

	/* Set the timer load count to the deadline. */
	ROTOR_MCU_TMR_TNLC(1) = ticks;

	/* Enable the timer. */
	ROTOR_MCU_TMR_TNCR(1) |= (1 << 0);
}

uint32_t __hw_clock_event_get(void)
{
	uint32_t ticks;
	/* Get the time of the next programmed deadline. */
	ticks = ROTOR_MCU_TMR_TNCV(1);
	return __hw_clock_source_read() + ((ticks * SECOND) / clock_get_freq());
}

void __hw_clock_event_clear(void)
{
	/* Disable the event timer.  This also clears any pending interrupt. */
	ROTOR_MCU_TMR_TNCR(1) &= ~(1 << 0);
}

/* Triggered when Timer 1 reaches 0. */
void __hw_clock_event_irq(void)
{
	/*
	 * Clear the event which disables the timer and clears the pending
	 * interrupt.
	 */
	__hw_clock_event_clear();

	/* Process timers now. */
	process_timers(0);
}
DECLARE_IRQ(ROTOR_MCU_IRQ_TIMER_1, __hw_clock_event_irq, 1);

uint32_t __hw_clock_source_read(void)
{
#ifdef BOARD_REI
	/* uint32_t ticks = reload_val - ROTOR_MCU_TMR_TNCV(0); */
	uint32_t ticks = 0xFFFFFFFF - ROTOR_MCU_TMR_TNCV(0);
	/* Convert the ticks into microseconds. */
	/* return ticks * (SECOND / clock_get_freq()); */

	return (ticks * (clock_get_freq() / SECOND));
#else
	/* Convert ticks to microseconds and account for the rollovers. */
	uint32_t ticks = 0xFFFFFFFF - ROTOR_MCU_TMR_TNCV(0);
	uint32_t us = (0xFFFFFFFF / clock_get_freq()) * rollover_cnt * SECOND;
	return us + ((ticks * SECOND) / clock_get_freq());
#endif /* defined(BOARD_REI) */
}

void __hw_clock_source_set(uint32_t ts)
{
	/* Convert microseconds to ticks. */
	uint32_t ticks;

	/* Disable the timer. */
	ROTOR_MCU_TMR_TNCR(0) &= ~1;
#ifdef BOARD_REI
	/* ticks = (ts / SECOND) * clock_get_freq(); */
	ticks = (clock_get_freq() / SECOND) * ts;
	/* Set the load count. */
	/* ROTOR_MCU_TMR_TNLC(0) = reload_val - ticks; */
	ROTOR_MCU_TMR_TNLC(0) = 0xFFFFFFFF - ticks;

	/* Re-enable the timer block. */
	ROTOR_MCU_TMR_TNCR(0) |= 1;

	/*
	 * Restore the behaviour of counting down from the reload value after
	 * the next reload.
	 */
	/* ROTOR_MCU_TMR_TNLC(0) = reload_val; */
#else
	/* Determine the rollover count and set the load count */
	rollover_cnt = ts / ((0xFFFFFFFF / clock_get_freq()) * SECOND);
	ticks = (ts % (0xFFFFFFFF / clock_get_freq()) * SECOND);
	ROTOR_MCU_TMR_TNLC(0) = 0xFFFFFFFF - ticks;

	/* Re-enable the timer block. */
	ROTOR_MCU_TMR_TNCR(0) |= 1;
#endif /* defined(BOARD_REI) */
}

/* Triggered when Timer 0 reaches 0. */
void __hw_clock_source_irq(void)
{
	/* Make sure that the interrupt actually fired. */
	if (!(ROTOR_MCU_TMR_TNIS(0) & (1 << 0)))
		return;
	/*
	 * Clear the interrupt by reading the TNEOI register.  Reading from this
	 * register returns all zeroes.
	 */
	if (ROTOR_MCU_TMR_TNEOI(0))
		;

#ifdef BOARD_REI
	/* Process timers indicating the overflow event. */
	process_timers(1);
#else
	rollover_cnt++;
	if (rollover_cnt >= (clock_get_freq() / 1000000)) {
		/* Process timers indicating the overflow event. */
		process_timers(1);
		rollover_cnt = 0;
	} else {
		process_timers(0);
	}
#endif /* defined(BOARD_REI) */
}
DECLARE_IRQ(ROTOR_MCU_IRQ_TIMER_0, __hw_clock_source_irq, 1);

void __hw_timer_enable_clock(int n, int enable)
{
	/* Should be already be configured. */
}

int __hw_clock_source_init(uint32_t start_t)
{
#ifdef BOARD_REI
	/* reload_val = (0xFFFFFFFF / SECOND) * clock_get_freq(); */
	/* reload_val = 0xFFFFFFFF; */
#endif /* defined(BOARD_REI) */
	/*
	 * Use Timer 0 as the clock.  The clock source for the timer block
	 * cannot be prescaled down to 1MHz, therefore, we'll have to handle the
	 * rollovers.
	 *
	 * There's also no match functionality, so set up an additional timer,
	 * Timer 1, to handle events.
	 */

	/* Disable the timers. */
	ROTOR_MCU_TMR_TNCR(0) &= ~(1 << 0);
	ROTOR_MCU_TMR_TNCR(1) &= ~(1 << 0);

	/*
	 * Timer 0
	 *
	 * Unmask interrupt, set user-defined count mode, and disable PWM.
	 */
	ROTOR_MCU_TMR_TNCR(0) = (1 << 1);

	/* Use the specified start timer value and start the timer. */
	__hw_clock_source_set(start_t);

	/*
	 * Timer 1
	 *
	 * Unmask interrupt, set user-defined count mode, and disable PWM.
	 */
	ROTOR_MCU_TMR_TNCR(1) = (1 << 1);

	/* Enable interrupts. */
	task_enable_irq(ROTOR_MCU_IRQ_TIMER_0);
	task_enable_irq(ROTOR_MCU_IRQ_TIMER_1);

	/* Return event timer IRQ number. */
	return ROTOR_MCU_IRQ_TIMER_1;
}
