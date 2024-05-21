/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @brief Tests initialization of global/static objects.
 */

#include "test_util.h"

#include <cstdio>
#include <cstring>

struct TestObj {
	TestObj()
	{
		/*
		 * In order to make sure the compiler can't perform the
		 * initialization at compile time (e.g., initialization of a
		 * POD value), we make a function call.
		 *
		 * In order to keep the test focused on initialization only
		 * and not the behavior of the heap, we avoid objects that
		 * would use dynamic allocation (such as std::vector).
		 *
		 * When making changes to this test, make sure to disable the
		 * call to call constructors of global objects
		 * (__libc_init_array) and make sure the test fails.
		 */
		snprintf(val, sizeof(val), "test %d", 42);
	}

	char val[8];
};

static TestObj global_obj;

static int test_global_init()
{
	TEST_EQ(strcmp(global_obj.val, "test 42"), 0, "%d");
	return EC_SUCCESS;
}

void run_test(int, const char **)
{
	test_reset();
	RUN_TEST(test_global_init);
	test_print_result();
}
