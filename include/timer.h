/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Timer module for Chrome EC operating system */

#ifndef __CROS_EC_TIMER_H
#define __CROS_EC_TIMER_H

#include "common.h"
#include "task_id.h"

/* Time units in microseconds */
#define MSEC         1000
#define SECOND    1000000
#define MINUTE   60000000
#define HOUR   3600000000ull  /* Too big to fit in a signed int */

/* Microsecond timestamp. */
typedef union {
	uint64_t val;
	struct {
		uint32_t lo;
		uint32_t hi;
	} le /* little endian words */;
} timestamp_t;

/* Data type for POSIX style clock() implementation */
typedef long clock_t;

/**
 * Initialize the timer module.
 */
void timer_init(void);

/**
 * Launch a one-shot timer for a task.
 *
 * Note that each task can have only a single active timer.
 *
 * @param tstamp	Expiration timestamp for timer
 * @param tskid		Task to set timer for
 *
 * @return EC_SUCCESS, or non-zero if error.
 */
int timer_arm(timestamp_t tstamp, task_id_t tskid);

/**
 * Cancel a running timer for the specified task id.
 */
void timer_cancel(task_id_t tskid);

/**
 * Check if a timestamp has passed / expired
 *
 * @param deadline	deadline timer value to check
 * @param now		pointer to value of time_now() if known, else NULL
 * @return 0 if deadline has not yet passed, 1 if it has passed
 */
int timestamp_expired(timestamp_t deadline, const timestamp_t *now);

/**
 * Busy-wait.
 *
 * This may be called with interrupts disabled, at any time after timer_init()
 * has been called.
 *
 * Note that calling this with us>1000 may impact system performance; use
 * usleep() for longer delays.
 *
 * @param us		Number of microseconds to delay.
 */
void udelay(unsigned us);

/**
 * Sleep.
 *
 * The current task will be de-scheduled for at least the specified delay (and
 * perhaps longer, if a higher-priority task is running when the delay
 * expires).
 *
 * This may only be called from a task function, with interrupts enabled.
 *
 * @param us		Number of microseconds to sleep.
 */
void usleep(unsigned us);

/**
 * Sleep for milliseconds.
 *
 * Otherwise the same as usleep().
 *
 * @param ms		Number of milliseconds to sleep.
 */
static inline void msleep(unsigned ms)
{
	usleep(ms * MSEC);
}

/**
 * Sleep for seconds
 *
 * Otherwise the same as usleep().
 *
 * @param sec		Number of seconds to sleep.
 */
static inline void sleep(unsigned sec)
{
	usleep(sec * SECOND);
}

/**
 * Get the current timestamp from the system timer.
 */
timestamp_t get_time(void);

/**
 * Force the current value of the system timer.
 *
 * This function is for the power management implementation which wants to fix
 * the system time when waking up from a mode with clocks turned off.
 *
 * Note: must be called with interrupts disabled.
 */
void force_time(timestamp_t ts);

/**
 * Print the current timer information using the command output channel.
 *
 * This may be called from interrupt level.
 */
void timer_print_info(void);

/**
 * Returns the number of microseconds that have elapsed from a start time.
 *
 * This function is for timing short delays typically of a few milliseconds
 * or so.
 *
 * Note that this is limited to a maximum of 32 bits, which is around an
 * hour. After that, the value returned will wrap.
 *
 * @param start		Start time to compare against
 * @return number of microseconds that have elspsed since that start time
 */
static inline unsigned time_since32(timestamp_t start)
{
	return get_time().le.lo - start.le.lo;
}

/**
 * Returns a free running millisecond clock counter, which matches tpm2
 * library expectations.
 */
clock_t clock(void);

/**
 * To compare time and deal with rollover
 *
 * Return true if a is after b.
 */
static inline int time_after(uint32_t a, uint32_t b)
{
	return (int32_t)(b - a) < 0;
}

#endif  /* __CROS_EC_TIMER_H */
