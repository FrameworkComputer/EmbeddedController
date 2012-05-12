/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Timer module for Chrome EC operating system */

#ifndef __CROS_EC_TIMER_H
#define __CROS_EC_TIMER_H

#include "common.h"
#include "task_id.h"

/* Microsecond timestamp. */
typedef union {
	uint64_t val;
	struct {
		uint32_t lo;
		uint32_t hi;
	} le /* little endian words */;
} timestamp_t;

/* Initializes the Timer module. */
int timer_init(void);

/* Launch a one-shot timer for task <tskid> which expires at timestamp
 * <tstamp>. */
int timer_arm(timestamp_t tstamp, task_id_t tskid);

/* Cancel a running timer for the specified task id. */
int timer_cancel(task_id_t tskid);

/**
 * Check if a timestamp has passed / expired
 *
 * @param deadline	deadline timer value to check
 * @param now		pointer to value of time_now() if known, else NULL
 * @return 0 if deadline has not yet passed, 1 if it has passed
 */
int timestamp_expired(timestamp_t deadline, const timestamp_t *now);

/* Busy-wait the selected number of microseconds.  Note that calling this
 * with us>1000 may impact system performance; use usleep for longer delays. */
void udelay(unsigned us);

/* Sleep during the selected number of microseconds.  The current task will be
 * de-scheduled until the delay expires.
 *
 * Note: if an event happens before the end of sleep, the function will return.
 */
void usleep(unsigned us);

/* Get the current timestamp from the system timer. */
timestamp_t get_time(void);

/* Print the current timer information using the command output channel.  This
 * may be called from interrupt level. */
void timer_print_info(void);

#endif  /* __CROS_EC_TIMER_H */
