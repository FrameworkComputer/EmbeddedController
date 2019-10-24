/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Timer module */

#include <stdint.h>
#include <stdio.h>

#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

static timestamp_t boot_time;
static int time_set;

void usleep(unsigned us)
{
	if (!task_start_called() || task_get_current() == TASK_ID_INVALID) {
		udelay(us);
		return;
	}

	ASSERT(!in_interrupt_context() &&
	       task_get_current() != TASK_ID_INT_GEN);

	task_wait_event(us);
}

timestamp_t _get_time(void)
{
	static timestamp_t time;

	/*
	 * We just monotonically increase the microsecond every time we check
	 * the time. Do not depend on host system time as this introduces
	 * flakyness in tests. The time is periodically fast forwarded with
	 * force_time() during the host's task scheduler implementation.
	 */
	++time.val;
	return time;
}

test_mockable timestamp_t get_time(void)
{
	timestamp_t ret = _get_time();
	ret.val -= boot_time.val;
	return ret;
}

uint32_t __hw_clock_source_read(void)
{
	return get_time().le.lo;
}

void force_time(timestamp_t ts)
{
	timestamp_t now = _get_time();
	boot_time.val = now.val - ts.val;
	time_set = 1;
}

void udelay(unsigned us)
{
	timestamp_t deadline;

	if (!in_interrupt_context() && task_get_current() == TASK_ID_INT_GEN) {
		interrupt_generator_udelay(us);
		return;
	}

	deadline.val = get_time().val + us;
	while (get_time().val < deadline.val)
		;
}

int timestamp_expired(timestamp_t deadline, const timestamp_t *now)
{
	timestamp_t now_val;

	if (!now) {
		now_val = get_time();
		now = &now_val;
	}

	return ((int64_t)(now->val - deadline.val) >= 0);
}

void timer_init(void)
{
	if (!time_set)
		boot_time = _get_time();
}
