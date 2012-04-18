/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hardware timer driver API */

#ifndef __CROS_EC_HWTIMER_H
#define __CROS_EC_HWTIMER_H

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

/**
 * Initializes the hardware timer used to provide clock services.
 *
 * It returns the IRQ number of the timer routine.
 */
int __hw_clock_source_init(void);

/**
 * Searches the next deadline and program it in the timer hardware.
 *
 * overflow: if true, the 32-bit counter as overflowed since the last call.
 *
 * This function is exported from the common timers code as an helper for the
 * hardware timer interrupt routine.
 */
void process_timers(int overflow);

#endif  /* __CROS_EC_HWTIMER_H */
