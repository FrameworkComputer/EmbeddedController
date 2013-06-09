/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test queue.
 */

#include "common.h"
#include "console.h"
#include "queue.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

static char buffer6[6];  /* Max 5 items in queue */
static struct queue test_queue6 = {
	.buf_bytes  = sizeof(buffer6),
	.unit_bytes = sizeof(char),
	.buf        = buffer6,
};

static char buffer5[5];  /* Max 2 items (2 byte for each) in queue */
static struct queue test_queue5 = {
	.buf_bytes  = sizeof(buffer5),
	.unit_bytes = 2,
	.buf        = buffer5,
};

#define LOOP_DEQUE(q, d, n) \
	do { \
		int i; \
		for (i = 0; i < n; ++i) \
			TEST_ASSERT(queue_remove_unit(&q, d + i)); \
	} while (0)

static int test_queue6_empty(void)
{
	char dummy = 1;

	queue_reset(&test_queue6);
	TEST_ASSERT(queue_is_empty(&test_queue6));
	queue_add_units(&test_queue6, &dummy, 1);
	TEST_ASSERT(!queue_is_empty(&test_queue6));

	return EC_SUCCESS;
}

static int test_queue6_reset(void)
{
	char dummy = 1;

	queue_reset(&test_queue6);
	queue_add_units(&test_queue6, &dummy, 1);
	queue_reset(&test_queue6);
	TEST_ASSERT(queue_is_empty(&test_queue6));

	return EC_SUCCESS;
}

static int test_queue6_fifo(void)
{
	char buf1[3] = {1, 2, 3};
	char buf2[3];

	queue_reset(&test_queue6);

	queue_add_units(&test_queue6, buf1 + 0, 1);
	queue_add_units(&test_queue6, buf1 + 1, 1);
	queue_add_units(&test_queue6, buf1 + 2, 1);

	LOOP_DEQUE(test_queue6, buf2, 3);
	TEST_ASSERT_ARRAY_EQ(buf1, buf2, 3);

	return EC_SUCCESS;
}

static int test_queue6_multiple_units_add(void)
{
	char buf1[5] = {1, 2, 3, 4, 5};
	char buf2[5];

	queue_reset(&test_queue6);
	TEST_ASSERT(queue_has_space(&test_queue6, 5));
	queue_add_units(&test_queue6, buf1, 5);
	LOOP_DEQUE(test_queue6, buf2, 5);
	TEST_ASSERT_ARRAY_EQ(buf1, buf2, 5);

	return EC_SUCCESS;
}

static int test_queue6_removal(void)
{
	char buf1[5] = {1, 2, 3, 4, 5};
	char buf2[5];

	queue_reset(&test_queue6);
	queue_add_units(&test_queue6, buf1, 5);
	/* 1, 2, 3, 4, 5 */
	LOOP_DEQUE(test_queue6, buf2, 3);
	TEST_ASSERT_ARRAY_EQ(buf1, buf2, 3);
	/* 4, 5 */
	queue_add_units(&test_queue6, buf1, 2);
	/* 4, 5, 1, 2 */
	TEST_ASSERT(queue_has_space(&test_queue6, 1));
	TEST_ASSERT(!queue_has_space(&test_queue6, 2));
	LOOP_DEQUE(test_queue6, buf2, 1);
	TEST_ASSERT(buf2[0] == 4);
	/* 5, 1, 2 */
	queue_add_units(&test_queue6, buf1 + 2, 2);
	/* 5, 1, 2, 3, 4 */
	TEST_ASSERT(!queue_has_space(&test_queue6, 1));
	LOOP_DEQUE(test_queue6, buf2, 1);
	TEST_ASSERT(buf2[0] == 5);
	LOOP_DEQUE(test_queue6, buf2, 4);
	TEST_ASSERT_ARRAY_EQ(buf1, buf2, 4);
	TEST_ASSERT(queue_is_empty(&test_queue6));
	/* Empty */
	queue_add_units(&test_queue6, buf1, 5);
	LOOP_DEQUE(test_queue6, buf2, 5);
	TEST_ASSERT_ARRAY_EQ(buf1, buf2, 5);

	return EC_SUCCESS;
}

static int test_queue5_odd_even(void)
{
	uint16_t buf1[3] = {1, 2, 3};
	uint16_t buf2[3];

	queue_reset(&test_queue5);
	queue_add_units(&test_queue5, buf1, 1);
	/* 1 */
	TEST_ASSERT(!queue_has_space(&test_queue5, 2));
	TEST_ASSERT(queue_has_space(&test_queue5, 1));
	queue_add_units(&test_queue5, buf1 + 1, 1);
	/* 1, 2 */
	TEST_ASSERT(!queue_has_space(&test_queue5, 1));
	LOOP_DEQUE(test_queue5, buf2, 2);
	TEST_ASSERT_ARRAY_EQ(buf1, buf2, 2);
	TEST_ASSERT(queue_is_empty(&test_queue5));
	/* Empty */
	TEST_ASSERT(!queue_has_space(&test_queue5, 3));
	TEST_ASSERT(queue_has_space(&test_queue5, 2));
	TEST_ASSERT(queue_has_space(&test_queue5, 1));
	queue_add_units(&test_queue5, buf1 + 2, 1);
	/* 3 */
	LOOP_DEQUE(test_queue5, buf2, 1);
	TEST_ASSERT(buf2[0] == 3);
	TEST_ASSERT(queue_is_empty(&test_queue5));

	return EC_SUCCESS;
}

void run_test(void)
{
	test_reset();

	RUN_TEST(test_queue6_empty);
	RUN_TEST(test_queue6_reset);
	RUN_TEST(test_queue6_fifo);
	RUN_TEST(test_queue6_multiple_units_add);
	RUN_TEST(test_queue6_removal);
	RUN_TEST(test_queue5_odd_even);

	test_print_result();
}
