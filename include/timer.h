/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Timer module for Chrome EC operating system */

#ifndef __EC_TIMER_H
#define __EC_TIMER_H

#include "common.h"
#include "task_id.h"

/* Micro-second timestamp. */
typedef union {
	uint64_t val;
	struct {
		uint32_t lo;
		uint32_t hi;
	} le /* little endian words */;
} timestamp_t;

/* Initializes the Timer module. */
int timer_init(void);

/**
 * Launches a one-shot timer.
 *
 * tstamp : timestamp in micro-seconds when the timer expires
 * tskid : identifier of the task owning the timer
 */
int timer_arm(timestamp_t tstamp, task_id_t tskid);

/**
 * Cancels a running timer.
 *
 * tskid : identifier of the task owning the timer
 */
int timer_cancel(task_id_t tskid);

/**
 * Busy wait the selected number of micro-seconds
 */
void udelay(unsigned us);

/**
 * Sleep during the selected number of micro-seconds
 *
 * The current task will be de-scheduled until the delay expired
 *
 * Note: if an event happens before the end of sleep, the function will return.
 */
void usleep(unsigned us);

/**
 * Get the current timestamp from the system timer
 */
timestamp_t get_time(void);

#endif  /* __EC_TIMER_H */
