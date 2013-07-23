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

/*
 * For test that need to test for longer than 10 seconds, adjust
 * its time scale in test/build.mk by specifying
 * <test_name>-scale=<new scale>.
 */
#ifndef TEST_TIME_SCALE
#define TEST_TIME_SCALE 1
#endif

static timestamp_t boot_time;
static int time_set;

void usleep(unsigned us)
{
	task_wait_event(us);
}

timestamp_t _get_time(void)
{
	struct timespec ts;
	timestamp_t ret;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	ret.val = (1000000000 * (uint64_t)ts.tv_sec + ts.tv_nsec) *
		  TEST_TIME_SCALE / 1000;
	return ret;
}

timestamp_t get_time(void)
{
	timestamp_t ret = _get_time();
	ret.val -= boot_time.val;
	return ret;
}

void force_time(timestamp_t ts)
{
	timestamp_t now = _get_time();
	boot_time.val = now.val - ts.val;
	time_set = 1;
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
	if (!time_set)
		boot_time = _get_time();
}
