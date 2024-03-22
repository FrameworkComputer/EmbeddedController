/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Timer module */

#include "builtin/assert.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

#include <stdint.h>
#include <stdio.h>

static timestamp_t boot_time;
static int time_set;

int usleep(unsigned int us)
{
	if (!task_start_called() || task_get_current() == TASK_ID_INVALID) {
		udelay(us);
		return 0;
	}

	ASSERT(!in_interrupt_context() &&
	       task_get_current() != TASK_ID_INT_GEN);

	task_wait_event(us);
	return 0;
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

void udelay(unsigned int us)
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
	if (!time_set) {
		/*
		 * Start the timer just before the 64-bit rollover to try
		 * and catch 32-bit rollover/truncation bugs.
		 */
		timestamp_t ts = { .val = 0xFFFFFFF0 };

		force_time(ts);
	}
}
