/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hardware timer driver API */

#ifndef __CROS_EC_HWTIMER_H
#define __CROS_EC_HWTIMER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Programs when the next timer should fire an interrupt.
 *
 * The deadline is ahead of the current counter (which may of course wrap) by
 * the number of microseconds until the interrupt should fire.
 *
 * @param deadline	timestamp of the event in microseconds
 */
void __hw_clock_event_set(uint32_t deadline);

/* Returns the timestamp of the next programed event */
uint32_t __hw_clock_event_get(void);

/* Cancel the next event programed by __hw_clock_event_set */
void __hw_clock_event_clear(void);

/**
 * Get the lower 32-bits of the free-running counter used as clock
 *
 * The counter resolution must be 1us, since udelay() relies on this.
 *
 * @return current counter value
 */
#ifdef CONFIG_HWTIMER_64BIT
__override_proto
#endif
	uint32_t
	__hw_clock_source_read(void);

/**
 * Override the lower 32-bits of the hardware counter
 *
 * The new value takes effect immediately and the counter continues counting
 * from there, assuming it is enabled
 *
 * @ts	Value to write
 */
void __hw_clock_source_set(uint32_t ts);

/**
 * Get the 64-bit value of the free-running counter used as clock,
 * only available when CONFIG_HWTIMER_64BIT is enabled.
 *
 * This function should only be used by common/timer.c or
 * chip-specific code, as get_time() abstracts the config option away.
 */
uint64_t __hw_clock_source_read64(void);

/**
 * Override the 64-bit value of the free-running counter used as
 * clock, only available when CONFIG_HWTIMER_64BIT is enabled.
 *
 * This function should only be used by common/timer.c or
 * chip-specific code, as force_time() abstracts the config option
 * away.
 */
void __hw_clock_source_set64(uint64_t timestamp);

/**
 * Enable clock to a timer.
 *
 * @param n		Timer number to enable/disable
 * @param enable	Enable (!=0) or disable (=0) clock to timer
 */
void __hw_timer_enable_clock(int n, int enable);

/**
 * Initializes the hardware timer used to provide clock services, using the
 * specified start timer value.
 *
 * It returns the IRQ number of the timer routine.
 */
int __hw_clock_source_init(uint32_t start_t);

/**
 * Initializes the hardware timer used to provide clock services, using the
 * specified start timer value (CONFIG_HWTIMER_64BIT enabled).
 *
 * It returns the IRQ number of the timer routine.
 */
int __hw_clock_source_init64(uint64_t start_t);

/**
 * Searches the next deadline and program it in the timer hardware.
 *
 * overflow: if true, the 32-bit counter as overflowed since the last
 * call. Goes unused if CONFIG_HWTIMER_64BIT is enabled.
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

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_HWTIMER_H */
