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

static struct queue const test_queue8 = QUEUE_NULL(8, char);
static struct queue const test_queue2 = QUEUE_NULL(2, int16_t);

static int test_queue8_empty(void)
{
	char dummy = 1;

	queue_init(&test_queue8);
	TEST_ASSERT(queue_is_empty(&test_queue8));
	TEST_ASSERT(!queue_remove_units(&test_queue8, &dummy, 1));
	TEST_ASSERT(queue_add_units(&test_queue8, &dummy, 1) == 1);
	TEST_ASSERT(!queue_is_empty(&test_queue8));

	return EC_SUCCESS;
}

static int test_queue8_init(void)
{
	char dummy = 1;

	queue_init(&test_queue8);
	TEST_ASSERT(queue_add_units(&test_queue8, &dummy, 1) == 1);
	queue_init(&test_queue8);
	TEST_ASSERT(queue_is_empty(&test_queue8));

	return EC_SUCCESS;
}

static int test_queue8_fifo(void)
{
	char buf1[3] = {1, 2, 3};
	char buf2[3];

	queue_init(&test_queue8);

	TEST_ASSERT(queue_add_units(&test_queue8, buf1 + 0, 1) == 1);
	TEST_ASSERT(queue_add_units(&test_queue8, buf1 + 1, 1) == 1);
	TEST_ASSERT(queue_add_units(&test_queue8, buf1 + 2, 1) == 1);

	TEST_ASSERT(queue_remove_units(&test_queue8, buf2, 3) == 3);
	TEST_ASSERT_ARRAY_EQ(buf1, buf2, 3);

	return EC_SUCCESS;
}

static int test_queue8_multiple_units_add(void)
{
	char buf1[5] = {1, 2, 3, 4, 5};
	char buf2[5];

	queue_init(&test_queue8);
	TEST_ASSERT(queue_space(&test_queue8) >= 5);
	TEST_ASSERT(queue_add_units(&test_queue8, buf1, 5) == 5);
	TEST_ASSERT(queue_remove_units(&test_queue8, buf2, 5) == 5);
	TEST_ASSERT_ARRAY_EQ(buf1, buf2, 5);

	return EC_SUCCESS;
}

static int test_queue8_removal(void)
{
	char buf1[5] = {1, 2, 3, 4, 5};
	char buf2[5];

	queue_init(&test_queue8);
	TEST_ASSERT(queue_add_units(&test_queue8, buf1, 5) == 5);
	/* 1, 2, 3, 4, 5 */
	TEST_ASSERT(queue_remove_units(&test_queue8, buf2, 3) == 3);
	TEST_ASSERT_ARRAY_EQ(buf1, buf2, 3);
	/* 4, 5 */
	TEST_ASSERT(queue_add_units(&test_queue8, buf1, 2) == 2);
	/* 4, 5, 1, 2 */
	TEST_ASSERT(queue_space(&test_queue8) == 4);
	TEST_ASSERT(queue_remove_units(&test_queue8, buf2, 1) == 1);
	TEST_ASSERT(buf2[0] == 4);
	/* 5, 1, 2 */
	TEST_ASSERT(queue_add_units(&test_queue8, buf1 + 2, 2) == 2);
	/* 5, 1, 2, 3, 4 */
	TEST_ASSERT(queue_space(&test_queue8) == 3);
	TEST_ASSERT(queue_add_units(&test_queue8, buf1 + 2, 3) == 3);
	/* 5, 1, 2, 3, 4, 3, 4, 5 */
	TEST_ASSERT(queue_space(&test_queue8) == 0);
	TEST_ASSERT(queue_remove_units(&test_queue8, buf2, 1) == 1);
	TEST_ASSERT(buf2[0] == 5);
	TEST_ASSERT(queue_remove_units(&test_queue8, buf2, 4) == 4);
	TEST_ASSERT_ARRAY_EQ(buf1, buf2, 4);
	TEST_ASSERT(queue_remove_units(&test_queue8, buf2, 3) == 3);
	TEST_ASSERT_ARRAY_EQ(buf1 + 2, buf2, 3);
	TEST_ASSERT(queue_is_empty(&test_queue8));
	/* Empty */
	TEST_ASSERT(queue_add_units(&test_queue8, buf1, 5) == 5);
	TEST_ASSERT(queue_remove_units(&test_queue8, buf2, 5) == 5);
	TEST_ASSERT_ARRAY_EQ(buf1, buf2, 5);

	return EC_SUCCESS;
}

static int test_queue8_peek(void)
{
	char buf1[5] = {1, 2, 3, 4, 5};
	char buf2[5];

	queue_init(&test_queue8);
	TEST_ASSERT(queue_add_units(&test_queue8, buf1, 5) == 5);
	/* 1, 2, 3, 4, 5 */
	TEST_ASSERT(queue_count(&test_queue8) == 5);
	TEST_ASSERT(queue_space(&test_queue8) == 3);
	TEST_ASSERT(queue_peek_units(&test_queue8, buf2, 2, 3) == 3);
	TEST_ASSERT_ARRAY_EQ(buf1 + 2, buf2, 3);
	TEST_ASSERT(queue_count(&test_queue8) == 5);
	TEST_ASSERT(queue_space(&test_queue8) == 3);

	return EC_SUCCESS;
}

static int test_queue2_odd_even(void)
{
	uint16_t buf1[3] = {1, 2, 3};
	uint16_t buf2[3];

	queue_init(&test_queue2);
	TEST_ASSERT(queue_add_units(&test_queue2, buf1, 1) == 1);
	/* 1 */
	TEST_ASSERT(queue_space(&test_queue2) == 1);
	TEST_ASSERT(queue_add_units(&test_queue2, buf1 + 1, 1) == 1);
	/* 1, 2 */
	TEST_ASSERT(queue_space(&test_queue2) == 0);
	TEST_ASSERT(queue_remove_units(&test_queue2, buf2, 2) == 2);
	TEST_ASSERT_ARRAY_EQ(buf1, buf2, 2);
	TEST_ASSERT(queue_is_empty(&test_queue2));
	/* Empty */
	TEST_ASSERT(queue_space(&test_queue2) == 2);
	TEST_ASSERT(queue_add_units(&test_queue2, buf1 + 2, 1) == 1);
	/* 3 */
	TEST_ASSERT(queue_remove_units(&test_queue2, buf2, 1) == 1);
	TEST_ASSERT(buf2[0] == 3);
	TEST_ASSERT(queue_is_empty(&test_queue2));

	return EC_SUCCESS;
}

void run_test(void)
{
	test_reset();

	RUN_TEST(test_queue8_empty);
	RUN_TEST(test_queue8_init);
	RUN_TEST(test_queue8_fifo);
	RUN_TEST(test_queue8_multiple_units_add);
	RUN_TEST(test_queue8_removal);
	RUN_TEST(test_queue8_peek);
	RUN_TEST(test_queue2_odd_even);

	test_print_result();
}
