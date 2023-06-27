/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gettimeofday.h"
#include "mock/timer_mock.h"
#include "test_util.h"

#include <stddef.h>

static int test_gettimeofday_zero(void)
{
	struct timeval tv;
	timestamp_t now;

	now.val = 0;
	set_time(now);

	TEST_EQ(ec_gettimeofday(&tv, NULL), EC_SUCCESS, "%d");
	TEST_EQ(tv.tv_sec, 0L, "%ld");
	TEST_EQ(tv.tv_usec, 0L, "%ld");

	return EC_SUCCESS;
}

static int test_gettimeofday_zero_seconds(void)
{
	struct timeval tv;
	timestamp_t now;

	now.val = 100;
	set_time(now);

	TEST_EQ(ec_gettimeofday(&tv, NULL), EC_SUCCESS, "%d");
	TEST_EQ(tv.tv_sec, 0L, "%ld");
	TEST_EQ(tv.tv_usec, 100L, "%ld");

	return EC_SUCCESS;
}

static int test_gettimeofday_nonzero_seconds(void)
{
	struct timeval tv;
	timestamp_t now;

	now.val = 1000001;
	set_time(now);

	TEST_EQ(ec_gettimeofday(&tv, NULL), EC_SUCCESS, "%d");
	TEST_EQ(tv.tv_sec, 1L, "%ld");
	TEST_EQ(tv.tv_usec, 1L, "%ld");

	return EC_SUCCESS;
}

static int test_gettimeofday_max(void)
{
	struct timeval tv;
	timestamp_t now;

	now.val = UINT64_MAX;
	set_time(now);

	TEST_EQ(ec_gettimeofday(&tv, NULL), EC_SUCCESS, "%d");
	TEST_EQ(tv.tv_sec, 18446744073709L, "%ld");
	TEST_EQ(tv.tv_usec, 551615L, "%ld");

	return EC_SUCCESS;
}

static int test_gettimeofday_null_arg(void)
{
	TEST_EQ(ec_gettimeofday(NULL, NULL), EC_ERROR_INVAL, "%d");
	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	/*
	 * Right now these tests only work on the host since they use a mock
	 * timer. Using a mock timer on device prevents the device from booting.
	 */
	test_reset();
	RUN_TEST(test_gettimeofday_zero);
	RUN_TEST(test_gettimeofday_zero_seconds);
	RUN_TEST(test_gettimeofday_nonzero_seconds);
	RUN_TEST(test_gettimeofday_max);
	RUN_TEST(test_gettimeofday_null_arg);
	test_print_result();
}
