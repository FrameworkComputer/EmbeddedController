/* Copyright 2013 The Chromium OS Authors. All rights reserved.
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
#include <stdio.h>

static struct queue const test_queue8 = QUEUE_NULL(8, char);
static struct queue const test_queue2 = QUEUE_NULL(2, int16_t);

static int test_queue8_empty(void)
{
	char tmp = 1;

	TEST_ASSERT(queue_is_empty(&test_queue8));
	TEST_ASSERT(!queue_remove_units(&test_queue8, &tmp, 1));
	TEST_ASSERT(queue_add_units(&test_queue8, &tmp, 1) == 1);
	TEST_ASSERT(!queue_is_empty(&test_queue8));

	return EC_SUCCESS;
}

static int test_queue8_init(void)
{
	char tmp = 1;

	TEST_ASSERT(queue_add_units(&test_queue8, &tmp, 1) == 1);
	queue_init(&test_queue8);
	TEST_ASSERT(queue_is_empty(&test_queue8));

	return EC_SUCCESS;
}

static int test_queue8_fifo(void)
{
	char buf1[3] = {1, 2, 3};
	char buf2[3];

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

static int test_queue8_chunks(void)
{
	static uint8_t const data[3] = {1, 2, 3};
	struct queue_chunk chunk;

	chunk = queue_get_write_chunk(&test_queue8, 0);

	TEST_ASSERT(chunk.count == 8);

	memcpy(chunk.buffer, data, 3);

	TEST_ASSERT(queue_advance_tail(&test_queue8, 3) == 3);

	chunk = queue_get_read_chunk(&test_queue8);

	TEST_ASSERT(chunk.count == 3);
	TEST_ASSERT_ARRAY_EQ((uint8_t *) chunk.buffer, data, 3);

	TEST_ASSERT(queue_advance_head(&test_queue8, 3) == 3);
	TEST_ASSERT(queue_is_empty(&test_queue8));

	return EC_SUCCESS;
}

static int test_queue8_chunks_wrapped(void)
{
	static uint8_t const data[3] = {1, 2, 3};

	/* Move near the end of the queue */
	TEST_ASSERT(queue_advance_tail(&test_queue8, 6) == 6);
	TEST_ASSERT(queue_advance_head(&test_queue8, 6) == 6);

	/* Add three units, causing the tail to wrap */
	TEST_ASSERT(queue_add_units(&test_queue8, data, 3) == 3);

	/*
	 * With a wrapped tail we should only be able to access the first two
	 * elements for reading, but all five free elements for writing.
	 */
	TEST_ASSERT(queue_get_read_chunk(&test_queue8).count == 2);
	TEST_ASSERT(queue_get_write_chunk(&test_queue8, 0).count == 5);

	/* Signal that we have read an element */
	TEST_ASSERT(queue_advance_head(&test_queue8, 1) == 1);

	/*
	 * Now we should only be able to see a single element for reading, but
	 * all six free element.
	 */
	TEST_ASSERT(queue_get_read_chunk(&test_queue8).count == 1);
	TEST_ASSERT(queue_get_write_chunk(&test_queue8, 0).count == 6);

	/* Signal that we have read the last two elements */
	TEST_ASSERT(queue_advance_head(&test_queue8, 2) == 2);

	/*
	 * Now there should be no elements available for reading, and only
	 * seven, not eight elements available for writing.  This is because
	 * the head/tail pointers now point to the second unit in the array.
	 */
	TEST_ASSERT(queue_get_read_chunk(&test_queue8).count == 0);
	TEST_ASSERT(queue_get_write_chunk(&test_queue8, 0).count == 7);

	return EC_SUCCESS;
}

static int test_queue8_chunks_full(void)
{
	static uint8_t const data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
	struct queue_chunk chunk;

	/* Move near the end of the queue */
	TEST_ASSERT(queue_advance_tail(&test_queue8, 6) == 6);
	TEST_ASSERT(queue_advance_head(&test_queue8, 6) == 6);

	/* Fill the queue */
	TEST_ASSERT(queue_add_units(&test_queue8, data, 8) == 8);

	/* With a full queue we shouldn't be able to write */
	TEST_ASSERT(queue_get_write_chunk(&test_queue8, 0).count == 0);

	/* But we should be able to read, though only two entries at first */
	chunk = queue_get_read_chunk(&test_queue8);

	TEST_ASSERT(chunk.count == 2);
	TEST_ASSERT_ARRAY_EQ((uint8_t *) chunk.buffer, data, 2);

	/* Signal that we have read both units */
	TEST_ASSERT(queue_advance_head(&test_queue8, 2) == 2);

	/* Now we should only be able to see the rest */
	chunk = queue_get_read_chunk(&test_queue8);

	TEST_ASSERT(chunk.count == 6);
	TEST_ASSERT_ARRAY_EQ((uint8_t *) chunk.buffer, data + 2, 6);


	return EC_SUCCESS;
}

static int test_queue8_chunks_empty(void)
{
	/* With an empty queue we shouldn't be able to read */
	TEST_ASSERT(queue_get_read_chunk(&test_queue8).count == 0);

	/* But we should be able to write, everything */
	TEST_ASSERT(queue_get_write_chunk(&test_queue8, 0).count == 8);

	return EC_SUCCESS;
}

static int test_queue8_chunks_advance(void)
{
	/*
	 * We should only be able to advance the tail (add units) as many
	 * units as there are in an empty queue.
	 */
	TEST_ASSERT(queue_advance_tail(&test_queue8, 10) == 8);

	/*
	 * Similarly, we should only be able to advance the head (remove
	 * units) as many units as there are in the now full queue.
	 */
	TEST_ASSERT(queue_advance_head(&test_queue8, 10) == 8);

	/*
	 * And it shouldn't matter if we start in the middle of the queue.
	 */
	TEST_ASSERT(queue_advance_tail(&test_queue8, 3) == 3);
	TEST_ASSERT(queue_advance_head(&test_queue8, 3) == 3);

	TEST_ASSERT(queue_advance_tail(&test_queue8, 10) == 8);
	TEST_ASSERT(queue_advance_head(&test_queue8, 10) == 8);

	return EC_SUCCESS;
}

static int test_queue8_chunks_offset(void)
{
	/* Check offsetting by 1 */
	TEST_ASSERT(queue_get_write_chunk(&test_queue8, 1).count == 7);
	TEST_ASSERT(queue_get_write_chunk(&test_queue8, 1).buffer ==
			test_queue8.buffer + 1);

	/* Check offsetting by 4 */
	TEST_ASSERT(queue_get_write_chunk(&test_queue8, 4).count == 4);
	TEST_ASSERT(queue_get_write_chunk(&test_queue8, 4).buffer ==
			test_queue8.buffer + 4);

	/* Check offset wrapping around */
	TEST_ASSERT(queue_get_write_chunk(&test_queue8, 10).count == 0);
	TEST_ASSERT(queue_get_write_chunk(&test_queue8, 10).buffer == NULL);

	/*
	 * Check offsetting when used memory is in the middle:
	 *    H T
	 * |--xx----|
	 */
	TEST_ASSERT(queue_advance_tail(&test_queue8, 4) == 4);
	TEST_ASSERT(queue_advance_head(&test_queue8, 2) == 2);

	/* Get writable chunk to right of tail. */
	TEST_ASSERT(queue_get_write_chunk(&test_queue8, 2).count == 2);
	TEST_ASSERT(queue_get_write_chunk(&test_queue8, 2).buffer ==
			test_queue8.buffer + 6);

	/* Get writable chunk wrapped and before head. */
	TEST_ASSERT(queue_get_write_chunk(&test_queue8, 4).count == 2);
	TEST_ASSERT(queue_get_write_chunk(&test_queue8, 4).buffer ==
			test_queue8.buffer);

	/* Check offsetting into non-writable memory. */
	TEST_ASSERT(queue_get_write_chunk(&test_queue8, 6).count == 0);
	TEST_ASSERT(queue_get_write_chunk(&test_queue8, 6).buffer == NULL);

	return EC_SUCCESS;
}

static int test_queue8_iterate_begin(void)
{
	struct queue const *q = &test_queue8;
	char data[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
	struct queue_iterator it;

	queue_begin(q, &it);
	TEST_EQ(it.ptr, NULL, "%p");

	queue_add_units(q, data, 4);
	queue_begin(q, &it);
	TEST_EQ(*((char *)it.ptr), 0, "%d");

	return EC_SUCCESS;
}

static int test_queue8_iterate_next(void)
{
	struct queue const *q = &test_queue8;
	char data[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
	struct queue_iterator it;

	queue_add_units(q, data, 4);
	queue_begin(q, &it);
	TEST_EQ(*((char *)it.ptr), 0, "%d");

	queue_next(q, &it);
	TEST_NE(it.ptr, NULL, "%p");
	TEST_EQ(*((char *)it.ptr), 1, "%d");

	queue_next(q, &it);
	TEST_NE(it.ptr, NULL, "%p");
	TEST_EQ(*((char *)it.ptr), 2, "%d");

	queue_next(q, &it);
	TEST_NE(it.ptr, NULL, "%p");
	TEST_EQ(*((char *)it.ptr), 3, "%d");

	queue_next(q, &it);
	TEST_EQ(it.ptr, NULL, "%p");

	return EC_SUCCESS;
}

static int test_queue2_iterate_next_full(void)
{
	struct queue const *q = &test_queue2;
	int16_t data[2] = { 523, -788 };
	struct queue_iterator it;

	queue_add_units(q, data, 2);
	queue_begin(q, &it);
	TEST_EQ(*((int16_t *)it.ptr), 523, "%d");

	queue_next(q, &it);
	TEST_NE(it.ptr, NULL, "%p");
	TEST_EQ(*((int16_t *)it.ptr), -788, "%d");

	queue_next(q, &it);
	TEST_EQ(it.ptr, NULL, "%p");

	return EC_SUCCESS;
}

static int test_queue8_iterate_next_reset_on_change(void)
{
	struct queue const *q = &test_queue8;
	char data[8] = { -88, -37, -5, -1, 3, 16, 56, 100 };
	struct queue_iterator it;

	queue_add_units(q, data, 4);
	queue_begin(q, &it);
	TEST_NE(it.ptr, NULL, "%p");
	queue_add_units(q, data + 4, 4);
	queue_next(q, &it);
	TEST_EQ(it.ptr, NULL, "%p");

	queue_begin(q, &it);
	TEST_NE(it.ptr, NULL, "%p");
	queue_advance_head(q, 3);
	queue_next(q, &it);
	TEST_EQ(it.ptr, NULL, "%p");

	return EC_SUCCESS;
}

void before_test(void)
{
	queue_init(&test_queue2);
	queue_init(&test_queue8);
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_queue8_empty);
	RUN_TEST(test_queue8_init);
	RUN_TEST(test_queue8_fifo);
	RUN_TEST(test_queue8_multiple_units_add);
	RUN_TEST(test_queue8_removal);
	RUN_TEST(test_queue8_peek);
	RUN_TEST(test_queue2_odd_even);
	RUN_TEST(test_queue8_chunks);
	RUN_TEST(test_queue8_chunks_wrapped);
	RUN_TEST(test_queue8_chunks_full);
	RUN_TEST(test_queue8_chunks_empty);
	RUN_TEST(test_queue8_chunks_advance);
	RUN_TEST(test_queue8_chunks_offset);
	RUN_TEST(test_queue8_iterate_begin);
	RUN_TEST(test_queue8_iterate_next);
	RUN_TEST(test_queue2_iterate_next_full);
	RUN_TEST(test_queue8_iterate_next_reset_on_change);

	test_print_result();
}
