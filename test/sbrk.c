/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "link_defs.h"
#include "shared_mem.h"
#include "test_util.h"

#include <errno.h>

#include <unistd.h>

test_static int test_sbrk_overflow(void)
{
	/* Requesting the maximum possible amount should succeed. */
	uint8_t *ptr = sbrk(shared_mem_size());
	TEST_NE(ptr, (void *)-1, "%p");

	/* Requesting any more should fail. */
	ptr = sbrk(1);
	TEST_EQ(ptr, (void *)-1, "%p");
	TEST_EQ(errno, ENOMEM, "%d");

	return EC_SUCCESS;
}

test_static int test_sbrk_underflow(void)
{
	uint8_t *const start = sbrk(0);
	TEST_EQ(start, (uint8_t *)__shared_mem_buf, "%p");

	/*
	 * We're already at the start of the shared mem buffer, so requesting
	 * less should fail.
	 */
	uint8_t *ptr = sbrk(-1);
	TEST_EQ(ptr, (void *)-1, "%p");
	TEST_EQ(errno, ENOMEM, "%d");

	ptr = sbrk(0);
	TEST_EQ(ptr, (uint8_t *)__shared_mem_buf, "%p");

	return EC_SUCCESS;
}

test_static int test_sbrk(void)
{
	uint8_t *const start = sbrk(0);
	if (!IS_ENABLED(BOARD_HOST))
		TEST_EQ(start, (uint8_t *)__shared_mem_buf, "%p");

	uint8_t *prev = sbrk(100);
	TEST_EQ(prev, start, "%p");

	uint8_t *cur = sbrk(0);
	TEST_EQ(cur, prev + 100, "%p");

	prev = sbrk(-100);
	TEST_EQ(prev, cur, "%p");

	cur = sbrk(0);
	TEST_EQ(cur, start, "%p");

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_sbrk);
	if (!IS_ENABLED(BOARD_HOST)) {
		if (IS_ENABLED(SECTION_IS_RW)) {
			ccprintf("The following tests only work in RO, since "
				 "RW performs dynamic memory allocation "
				 "before the test starts.\n");
			test_fail();
			return;
		}
		RUN_TEST(test_sbrk_underflow);
		RUN_TEST(test_sbrk_overflow);
	}

	test_print_result();
}
