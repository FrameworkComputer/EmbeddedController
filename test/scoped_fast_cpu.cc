/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Basic test of ScopedFastCpu.
 */

#include "common.h"
#include "console.h"
#include "mock/clock_mock.h"
#include "scoped_fast_cpu.h"
#include "test_util.h"

test_static int fast_cpu_disable_at_start()
{
	{
		TEST_EQ(get_mock_fast_cpu_status(), 0, "%d");
		{
			/* instantiate, which calls constructor. */
			ScopedFastCpu cpu;
			TEST_EQ(get_mock_fast_cpu_status(), 1, "%d");
			/* destructed here. */
		}
		TEST_EQ(get_mock_fast_cpu_status(), 0, "%d");
	}
	return EC_SUCCESS;
}

test_static int fast_cpu_enable_at_start()
{
	{
		ScopedFastCpu cpu;
		TEST_EQ(get_mock_fast_cpu_status(), 1, "%d");
		{
			/* instantiate, which calls constructor. */
			ScopedFastCpu cpu;
			TEST_EQ(get_mock_fast_cpu_status(), 1, "%d");
			/* destructed here. */
		}
		TEST_EQ(get_mock_fast_cpu_status(), 1, "%d");
	}
	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(fast_cpu_disable_at_start);
	RUN_TEST(fast_cpu_enable_at_start);

	test_print_result();
}