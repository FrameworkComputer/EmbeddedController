/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hardware timer driver API */

#ifndef __CROS_EC_HWTIMER_H
#define __CROS_EC_HWTIMER_H

struct timer_ctlr {
	unsigned cr1;
	unsigned cr2;
	unsigned smcr;
	unsigned dier;

	unsigned sr;
	unsigned egr;
	unsigned ccmr1;
	unsigned ccmr2;

	unsigned ccer;
	unsigned cnt;
	unsigned psc;
	unsigned arr;

	unsigned reserved30;
	unsigned ccr1;
	unsigned ccr2;
	unsigned ccr3;

	unsigned ccr4;
	unsigned reserved44;
	unsigned dcr;
	unsigned dmar;

	unsigned or;
};

/**
 * Programs when the next timer should fire an interrupt.
 * deadline: timestamp of the event.
 */
void __hw_clock_event_set(uint32_t deadline);

/* Returns the timestamp of the next programed event */
uint32_t __hw_clock_event_get(void);

/* Cancel the next event programed by __hw_clock_event_set */
void __hw_clock_event_clear(void);

/* Returns the value of the free-running counter used as clock. */
uint32_t __hw_clock_source_read(void);

/* Override the current value of the hardware counter */
void __hw_clock_source_set(uint32_t ts);

/**
 * Initializes the hardware timer used to provide clock services, using the
 * specified start timer value.
 *
 * It returns the IRQ number of the timer routine.
 */
int __hw_clock_source_init(uint32_t start_t);

/**
 * Searches the next deadline and program it in the timer hardware.
 *
 * overflow: if true, the 32-bit counter as overflowed since the last call.
 *
 * This function is exported from the common timers code as an helper for the
 * hardware timer interrupt routine.
 */
void process_timers(int overflow);

/**
 * Set up the timer that we will use as a watchdog warning.
 *
 * Once this has been set up, we will print a warning shortly before the
 * real watchdog fires. To avoid this, hwtimer_reset_watchdog() must be
 * called periodically.
 *
 * This is needed since the real watchdog timer (IWDG) does not provide
 * an interrupt to warn of an impending watchdog reset.
 */
void hwtimer_setup_watchdog(void);

/* Reset the watchdog timer, to avoid the watchdog warning */
void hwtimer_reset_watchdog(void);

#endif  /* __CROS_EC_HWTIMER_H */
