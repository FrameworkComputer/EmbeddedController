/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Timer module */

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "task.h"
#include "timer.h"

static timestamp_t boot_time;

void usleep(unsigned us)
{
	task_wait_event(us);
}

timestamp_t _get_time(void)
{
	struct timespec ts;
	timestamp_t ret;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	ret.val = 1000000 * (uint64_t)ts.tv_sec + ts.tv_nsec / 1000;
	return ret;
}

timestamp_t get_time(void)
{
	timestamp_t ret = _get_time();
	ret.val -= boot_time.val;
	return ret;
}

void udelay(unsigned us)
{
	timestamp_t deadline = get_time();
	deadline.val += us;
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
	boot_time = _get_time();
}
