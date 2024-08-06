/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "queue.h"
#include "timer.h"
#include "util.h"

#include <zephyr/ztest.h>

static struct queue const test_queue8 = QUEUE_NULL(8, char);
static struct queue const test_queue2 = QUEUE_NULL(2, int16_t);

static void queue_before(void *fixture)
{
	queue_init(&test_queue2);
	queue_init(&test_queue8);
}

ZTEST_SUITE(queue, NULL, NULL, queue_before, NULL, NULL);

ZTEST(queue, test_queue8_empty)
{
	char tmp = 1;

	zassert_true(queue_is_empty(&test_queue8));
	zassert_true(!queue_remove_units(&test_queue8, &tmp, 1));
	zassert_true(queue_add_units(&test_queue8, &tmp, 1) == 1);
	zassert_true(!queue_is_empty(&test_queue8));
}

ZTEST(queue, test_queue8_init)
{
	char tmp = 1;

	zassert_true(queue_add_units(&test_queue8, &tmp, 1) == 1);
	queue_init(&test_queue8);
	zassert_true(queue_is_empty(&test_queue8));
	zassert_true(queue_remove_unit(&test_queue8, &tmp) == 0);
}

ZTEST(queue, test_queue8_fifo)
{
	char buf1[3] = { 1, 2, 3 };
	char buf2[3];

	zassert_true(queue_add_units(&test_queue8, buf1 + 0, 1) == 1);
	zassert_true(queue_add_units(&test_queue8, buf1 + 1, 1) == 1);
	zassert_true(queue_add_units(&test_queue8, buf1 + 2, 1) == 1);

	zassert_true(queue_remove_units(&test_queue8, buf2, 3) == 3);
	zassert_mem_equal(buf1, buf2, 3);
}

ZTEST(queue, test_queue8_multiple_units_add)
{
	char buf1[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	char buf2[5];

	zassert_true(queue_space(&test_queue8) >= 5);
	zassert_true(queue_add_units(&test_queue8, buf1, 5) == 5);
	zassert_true(queue_remove_units(&test_queue8, buf2, 5) == 5);
	zassert_mem_equal(buf1, buf2, 5);

	zassert_true(queue_add_units(&test_queue8, buf1, 8) == 8);
	zassert_true(queue_add_unit(&test_queue8, &buf1[8]) == 0);
}

ZTEST(queue, test_queue8_removal)
{
	char buf1[5] = { 1, 2, 3, 4, 5 };
	char buf2[5];

	zassert_true(queue_add_units(&test_queue8, buf1, 5) == 5);
	/* 1, 2, 3, 4, 5 */
	zassert_true(queue_remove_units(&test_queue8, buf2, 3) == 3);
	zassert_mem_equal(buf1, buf2, 3);
	/* 4, 5 */
	zassert_true(queue_add_units(&test_queue8, buf1, 2) == 2);
	/* 4, 5, 1, 2 */
	zassert_true(queue_space(&test_queue8) == 4);
	zassert_true(queue_remove_units(&test_queue8, buf2, 1) == 1);
	zassert_true(buf2[0] == 4);
	/* 5, 1, 2 */
	zassert_true(queue_add_units(&test_queue8, buf1 + 2, 2) == 2);
	/* 5, 1, 2, 3, 4 */
	zassert_true(queue_space(&test_queue8) == 3);
	zassert_true(queue_add_units(&test_queue8, buf1 + 2, 3) == 3);
	/* 5, 1, 2, 3, 4, 3, 4, 5 */
	zassert_true(queue_space(&test_queue8) == 0);
	zassert_true(queue_remove_units(&test_queue8, buf2, 1) == 1);
	zassert_true(buf2[0] == 5);
	zassert_true(queue_remove_units(&test_queue8, buf2, 4) == 4);
	zassert_mem_equal(buf1, buf2, 4);
	zassert_true(queue_remove_units(&test_queue8, buf2, 3) == 3);
	zassert_mem_equal(buf1 + 2, buf2, 3);
	zassert_true(queue_is_empty(&test_queue8));
	/* Empty */
	zassert_true(queue_add_units(&test_queue8, buf1, 5) == 5);
	zassert_true(queue_remove_units(&test_queue8, buf2, 5) == 5);
	zassert_mem_equal(buf1, buf2, 5);
}

ZTEST(queue, test_queue8_peek)
{
	char buf1[5] = { 1, 2, 3, 4, 5 };
	char buf2[5];

	zassert_true(queue_add_units(&test_queue8, buf1, 5) == 5);
	/* 1, 2, 3, 4, 5 */
	zassert_true(queue_count(&test_queue8) == 5);
	zassert_true(queue_space(&test_queue8) == 3);
	zassert_true(queue_peek_units(&test_queue8, buf2, 2, 3) == 3);
	zassert_mem_equal(buf1 + 2, buf2, 3);
	zassert_true(queue_count(&test_queue8) == 5);
	zassert_true(queue_space(&test_queue8) == 3);
}

ZTEST(queue, test_queue2_odd_even)
{
	uint16_t buf1[3] = { 1, 2, 3 };
	uint16_t buf2[3];

	zassert_true(queue_add_units(&test_queue2, buf1, 1) == 1);
	/* 1 */
	zassert_true(queue_space(&test_queue2) == 1);
	zassert_true(queue_add_units(&test_queue2, buf1 + 1, 1) == 1);
	/* 1, 2 */
	zassert_true(queue_space(&test_queue2) == 0);
	zassert_true(queue_remove_units(&test_queue2, buf2, 2) == 2);
	zassert_mem_equal(buf1, buf2, 2);
	zassert_true(queue_is_empty(&test_queue2));
	/* Empty */
	zassert_true(queue_space(&test_queue2) == 2);
	zassert_true(queue_add_units(&test_queue2, buf1 + 2, 1) == 1);
	/* 3 */
	zassert_true(queue_remove_units(&test_queue2, buf2, 1) == 1);
	zassert_true(buf2[0] == 3);
	zassert_true(queue_is_empty(&test_queue2));
}

ZTEST(queue, test_queue8_chunks)
{
	static uint8_t const data[3] = { 1, 2, 3 };
	struct queue_chunk chunk;

	chunk = queue_get_write_chunk(&test_queue8, 0);

	zassert_true(chunk.count == 8);

	memcpy(chunk.buffer, data, 3);

	zassert_true(queue_advance_tail(&test_queue8, 3) == 3);

	chunk = queue_get_read_chunk(&test_queue8);

	zassert_true(chunk.count == 3);
	zassert_mem_equal((uint8_t *)chunk.buffer, data, 3);

	zassert_true(queue_advance_head(&test_queue8, 3) == 3);
	zassert_true(queue_is_empty(&test_queue8));
}

ZTEST(queue, test_queue8_chunks_wrapped)
{
	static uint8_t const data[3] = { 1, 2, 3 };

	/* Move near the end of the queue */
	zassert_true(queue_advance_tail(&test_queue8, 6) == 6);
	zassert_true(queue_advance_head(&test_queue8, 6) == 6);

	/* Add three units, causing the tail to wrap */
	zassert_true(queue_add_units(&test_queue8, data, 3) == 3);

	/*
	 * With a wrapped tail we should only be able to access the first two
	 * elements for reading, but all five free elements for writing.
	 */
	zassert_true(queue_get_read_chunk(&test_queue8).count == 2);
	zassert_true(queue_get_write_chunk(&test_queue8, 0).count == 5);

	/* Signal that we have read an element */
	zassert_true(queue_advance_head(&test_queue8, 1) == 1);

	/*
	 * Now we should only be able to see a single element for reading, but
	 * all six free element.
	 */
	zassert_true(queue_get_read_chunk(&test_queue8).count == 1);
	zassert_true(queue_get_write_chunk(&test_queue8, 0).count == 6);

	/* Signal that we have read the last two elements */
	zassert_true(queue_advance_head(&test_queue8, 2) == 2);

	/*
	 * Now there should be no elements available for reading, and only
	 * seven, not eight elements available for writing.  This is because
	 * the head/tail pointers now point to the second unit in the array.
	 */
	zassert_true(queue_get_read_chunk(&test_queue8).count == 0);
	zassert_true(queue_get_write_chunk(&test_queue8, 0).count == 7);
}

ZTEST(queue, test_queue8_chunks_full)
{
	static uint8_t const data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	struct queue_chunk chunk;

	/* Move near the end of the queue */
	zassert_true(queue_advance_tail(&test_queue8, 6) == 6);
	zassert_true(queue_advance_head(&test_queue8, 6) == 6);

	/* Fill the queue */
	zassert_true(queue_add_units(&test_queue8, data, 8) == 8);

	/* With a full queue we shouldn't be able to write */
	zassert_true(queue_get_write_chunk(&test_queue8, 0).count == 0);

	/* But we should be able to read, though only two entries at first */
	chunk = queue_get_read_chunk(&test_queue8);

	zassert_true(chunk.count == 2);
	zassert_mem_equal((uint8_t *)chunk.buffer, data, 2);

	/* Signal that we have read both units */
	zassert_true(queue_advance_head(&test_queue8, 2) == 2);

	/* Now we should only be able to see the rest */
	chunk = queue_get_read_chunk(&test_queue8);

	zassert_true(chunk.count == 6);
	zassert_mem_equal((uint8_t *)chunk.buffer, data + 2, 6);
}

ZTEST(queue, test_queue8_chunks_empty)
{
	/* With an empty queue we shouldn't be able to read */
	zassert_true(queue_get_read_chunk(&test_queue8).count == 0);

	/* But we should be able to write, everything */
	zassert_true(queue_get_write_chunk(&test_queue8, 0).count == 8);
}

ZTEST(queue, test_queue8_chunks_advance)
{
	/*
	 * We should only be able to advance the tail (add units) as many
	 * units as there are in an empty queue.
	 */
	zassert_true(queue_advance_tail(&test_queue8, 10) == 8);

	/*
	 * Similarly, we should only be able to advance the head (remove
	 * units) as many units as there are in the now full queue.
	 */
	zassert_true(queue_advance_head(&test_queue8, 10) == 8);

	/*
	 * And it shouldn't matter if we start in the middle of the queue.
	 */
	zassert_true(queue_advance_tail(&test_queue8, 3) == 3);
	zassert_true(queue_advance_head(&test_queue8, 3) == 3);

	zassert_true(queue_advance_tail(&test_queue8, 10) == 8);
	zassert_true(queue_advance_head(&test_queue8, 10) == 8);
}

ZTEST(queue, test_queue8_chunks_offset)
{
	/* Check offsetting by 1 */
	zassert_true(queue_get_write_chunk(&test_queue8, 1).count == 7);
	zassert_true(queue_get_write_chunk(&test_queue8, 1).buffer ==
		     test_queue8.buffer + 1);

	/* Check offsetting by 4 */
	zassert_true(queue_get_write_chunk(&test_queue8, 4).count == 4);
	zassert_true(queue_get_write_chunk(&test_queue8, 4).buffer ==
		     test_queue8.buffer + 4);

	/* Check offset wrapping around */
	zassert_true(queue_get_write_chunk(&test_queue8, 10).count == 0);
	zassert_true(queue_get_write_chunk(&test_queue8, 10).buffer == NULL);

	/*
	 * Check offsetting when used memory is in the middle:
	 *    H T
	 * |--xx----|
	 */
	zassert_true(queue_advance_tail(&test_queue8, 4) == 4);
	zassert_true(queue_advance_head(&test_queue8, 2) == 2);

	/* Get writable chunk to right of tail. */
	zassert_true(queue_get_write_chunk(&test_queue8, 2).count == 2);
	zassert_true(queue_get_write_chunk(&test_queue8, 2).buffer ==
		     test_queue8.buffer + 6);

	/* Get writable chunk wrapped and before head. */
	zassert_true(queue_get_write_chunk(&test_queue8, 4).count == 2);
	zassert_true(queue_get_write_chunk(&test_queue8, 4).buffer ==
		     test_queue8.buffer);

	/* Check offsetting into non-writable memory. */
	zassert_true(queue_get_write_chunk(&test_queue8, 6).count == 0);
	zassert_true(queue_get_write_chunk(&test_queue8, 6).buffer == NULL);
}

ZTEST(queue, test_queue8_iterate_begin)
{
	struct queue const *q = &test_queue8;
	char data[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
	struct queue_iterator it;

	queue_begin(q, &it);
	zassert_equal(it.ptr, NULL);

	queue_add_units(q, data, 4);
	queue_begin(q, &it);
	zassert_equal(*((char *)it.ptr), 0);
}

ZTEST(queue, test_queue8_iterate_next)
{
	struct queue const *q = &test_queue8;
	char data[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
	struct queue_iterator it;

	queue_add_units(q, data, 4);
	queue_begin(q, &it);
	zassert_equal(*((char *)it.ptr), 0);

	queue_next(q, &it);
	zassert_not_equal(it.ptr, NULL);
	zassert_equal(*((char *)it.ptr), 1);

	queue_next(q, &it);
	zassert_not_equal(it.ptr, NULL);
	zassert_equal(*((char *)it.ptr), 2);

	queue_next(q, &it);
	zassert_not_equal(it.ptr, NULL);
	zassert_equal(*((char *)it.ptr), 3);

	queue_next(q, &it);
	zassert_equal(it.ptr, NULL);
}

ZTEST(queue, test_queue2_iterate_next_full)
{
	struct queue const *q = &test_queue2;
	int16_t data[2] = { 523, -788 };
	struct queue_iterator it;

	queue_add_units(q, data, 2);
	queue_begin(q, &it);
	zassert_equal(*((int16_t *)it.ptr), 523);

	queue_next(q, &it);
	zassert_not_equal(it.ptr, NULL);
	zassert_equal(*((int16_t *)it.ptr), -788);

	queue_next(q, &it);
	zassert_equal(it.ptr, NULL);

	queue_next(q, &it);
	zassert_equal(it.ptr, NULL);
}

ZTEST(queue, test_queue8_iterate_next_reset_on_change)
{
	struct queue const *q = &test_queue8;
	char data[8] = { -88, -37, -5, -1, 3, 16, 56, 100 };
	struct queue_iterator it;

	queue_add_units(q, data, 4);
	queue_begin(q, &it);
	zassert_not_equal(it.ptr, NULL);
	queue_add_units(q, data + 4, 4);
	queue_next(q, &it);
	zassert_equal(it.ptr, NULL);

	queue_begin(q, &it);
	zassert_not_equal(it.ptr, NULL);
	queue_advance_head(q, 3);
	queue_next(q, &it);
	zassert_equal(it.ptr, NULL);
}
