/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "preserved_ring_buf.h"

#include <zephyr/ztest.h>

#define WORD_MASK(word_size) (-1UL >> (32 - word_size * 8))

/* Read and write to buffer in various ways and confirm contents match expected
 */
static void read_write_test(const preserved_ring_buf_t *buf, const int capacity,
			    const int word_size)
{
	preserved_ring_buf_reset(buf);

	/* Check properties */
	zassert_equal(buf->constants.word_size, word_size);
	zassert_equal(buf->properties->word_size, word_size);
	zassert_equal(buf->constants.capacity, capacity);
	zassert_equal(buf->properties->capacity, capacity);

	/* Verify buffer is empty initially */
	zassert_equal(preserved_ring_buf_len(buf), 0);
	for (int i = 0; i < capacity; i++)
		zassert_equal(0x00, preserved_ring_buf_read(buf, i));
	zassert_true(preserved_ring_buf_verify(buf));

	/* Fill half the buffer with i */
	for (int i = 0; i < capacity / 2; i++) {
		zassert_equal(preserved_ring_buf_len(buf), i);
		preserved_ring_buf_write(buf, i);
		zassert_equal(i & WORD_MASK(word_size),
			      preserved_ring_buf_read(buf, i));
	}

	/* Verify contents */
	zassert_equal(preserved_ring_buf_len(buf), capacity / 2);
	for (int i = 0; i < capacity / 2; i++)
		zassert_equal(i & WORD_MASK(word_size),
			      preserved_ring_buf_read(buf, i));
	for (int i = capacity / 2; i < capacity; i++)
		zassert_equal(0x00, preserved_ring_buf_read(buf, i));
	zassert_true(preserved_ring_buf_verify(buf));

	/* Fill second half of the buffer with i */
	for (int i = capacity / 2; i < capacity; i++) {
		zassert_equal(preserved_ring_buf_len(buf), i);
		preserved_ring_buf_write(buf, i);
		zassert_equal(i & WORD_MASK(word_size),
			      preserved_ring_buf_read(buf, i));
	}

	/* Verify contents again */
	zassert_equal(preserved_ring_buf_len(buf), capacity);
	for (int i = 0; i < capacity; i++)
		zassert_equal(i & WORD_MASK(word_size),
			      preserved_ring_buf_read(buf, i));
	zassert_true(preserved_ring_buf_verify(buf));

	/* Fill half of the buffer with -1 */
	for (int i = 0; i < capacity / 2; i++) {
		zassert_equal(preserved_ring_buf_len(buf), capacity);
		preserved_ring_buf_write(buf, -1);
	}

	/* Verify contents one last time */
	zassert_equal(preserved_ring_buf_len(buf), capacity);
	for (int i = 0; i < capacity / 2; i++)
		zassert_equal((i + capacity / 2) & WORD_MASK(word_size),
			      preserved_ring_buf_read(buf, i));
	for (int i = capacity / 2; i < capacity; i++)
		zassert_equal(-1 & WORD_MASK(word_size),
			      preserved_ring_buf_read(buf, i));
	zassert_true(preserved_ring_buf_verify(buf));
}

ZTEST_USER(preserved_ring_buf, test_read_write_uint8)
{
	DECLARE_PRESERVED_RING_BUF(uint8_t, buf, 1024, 1);
	read_write_test(buf, 1024, 1);
}

ZTEST_USER(preserved_ring_buf, test_read_write_uint16)
{
	DECLARE_PRESERVED_RING_BUF(uint16_t, buf, 1024, 1);
	read_write_test(buf, 1024, 2);
}

ZTEST_USER(preserved_ring_buf, test_read_write_uint32)
{
	DECLARE_PRESERVED_RING_BUF(uint32_t, buf, 1024, 1);
	read_write_test(buf, 1024, 4);
}

/* Corrupt the buffer in various ways and confirm verify fails */
ZTEST_USER(preserved_ring_buf, test_corrupt)
{
	DECLARE_PRESERVED_RING_BUF(uint16_t, buf, 16, 1);

	preserved_ring_buf_reset(buf);
	zassert_true(preserved_ring_buf_verify(buf));
	buf->buffer.as_uint16[0] = -1;
	zassert_false(preserved_ring_buf_verify(buf));

	preserved_ring_buf_reset(buf);
	zassert_true(preserved_ring_buf_verify(buf));
	buf->properties->checksum = -1;
	zassert_false(preserved_ring_buf_verify(buf));

	preserved_ring_buf_reset(buf);
	zassert_true(preserved_ring_buf_verify(buf));
	buf->properties->capacity -= 1;
	zassert_false(preserved_ring_buf_verify(buf));

	preserved_ring_buf_reset(buf);
	zassert_true(preserved_ring_buf_verify(buf));
	buf->properties->version -= 1;
	zassert_false(preserved_ring_buf_verify(buf));

	preserved_ring_buf_reset(buf);
	zassert_true(preserved_ring_buf_verify(buf));
	buf->properties->word_size -= 1;
	zassert_false(preserved_ring_buf_verify(buf));
}

ZTEST_USER(preserved_ring_buf, test_reset)
{
	const int capacity = 16;
	DECLARE_PRESERVED_RING_BUF(uint16_t, buf, 16, 1);
	preserved_ring_buf_reset(buf);

	/* Fill buffer and verify it's filled */
	for (int i = 0; i < capacity; i++)
		preserved_ring_buf_write(buf, i);
	zassert_equal(preserved_ring_buf_len(buf), capacity);
	for (int i = 0; i < capacity; i++)
		zassert_equal(i, preserved_ring_buf_read(buf, i));
	zassert_true(preserved_ring_buf_verify(buf));

	/* Reset and verify it's zero'd */
	preserved_ring_buf_reset(buf);
	zassert_equal(preserved_ring_buf_len(buf), 0);
	for (int i = 0; i < capacity; i++)
		zassert_equal(0, preserved_ring_buf_read(buf, i));
	zassert_true(preserved_ring_buf_verify(buf));
}

/* Verify that reading out of bounds just wraps */
ZTEST_USER(preserved_ring_buf, test_read_out_of_bounds)
{
	const int capacity = 16;
	const int word_size = 2;
	DECLARE_PRESERVED_RING_BUF(uint16_t, buf, 16, 1);
	preserved_ring_buf_reset(buf);

	/* Everything should be zero */
	for (int i = 0; i < capacity * 10; i++)
		zassert_equal(0, preserved_ring_buf_read(buf, i));

	/* Fill buffer */
	for (int i = 0; i < capacity; i++)
		preserved_ring_buf_write(buf, i);

	/* Verify reading wraps */
	zassert_equal(preserved_ring_buf_len(buf), capacity);
	for (int i = 0; i < capacity * 10; i++)
		zassert_equal((i % capacity) & WORD_MASK(word_size),
			      preserved_ring_buf_read(buf, i));
	zassert_equal(preserved_ring_buf_len(buf), capacity);

	zassert_true(preserved_ring_buf_verify(buf));
}

ZTEST_USER(preserved_ring_buf, test_head_wrap)
{
	const uint32_t capacity = 16;
	DECLARE_PRESERVED_RING_BUF(uint16_t, buf, 16, 1);
	preserved_ring_buf_reset(buf);

	/* Simulate head incrementing near UINT32_MAX */
	buf->properties->head = UINT32_MAX - capacity + 1;

	/* Check that buffer appears valid and full */
	zassert_true(preserved_ring_buf_verify(buf));
	zassert_equal(preserved_ring_buf_len(buf), capacity);

	/* Write until head wraps */
	for (int i = 0; i < capacity; i++) {
		zassert_equal(preserved_ring_buf_len(buf), capacity);
		preserved_ring_buf_write(buf, i);
	}

	/* Verify buffer is truncated, but still valid after head wrap */
	zassert_equal(preserved_ring_buf_len(buf), 0);
	zassert_true(preserved_ring_buf_verify(buf));

	/* Refill buffer and verify */
	for (int i = 0; i < capacity; i++) {
		zassert_equal(preserved_ring_buf_len(buf), i);
		preserved_ring_buf_write(buf, i);
	}
	zassert_equal(preserved_ring_buf_len(buf), capacity);
	zassert_true(preserved_ring_buf_verify(buf));
}

ZTEST_USER(preserved_ring_buf, test_version)
{
	const int ring_buf_version = 0x12345678;
	DECLARE_PRESERVED_RING_BUF(uint16_t, buf, 16, ring_buf_version);
	preserved_ring_buf_reset(buf);
	zassert_equal(preserved_ring_buf_version(buf), ring_buf_version);
}

ZTEST_USER(preserved_ring_buf, test_capacity)
{
	DECLARE_PRESERVED_RING_BUF(uint16_t, buf, 16, 1);
	preserved_ring_buf_reset(buf);
	zassert_equal(preserved_ring_buf_capacity(buf), 16);
}

ZTEST_SUITE(preserved_ring_buf, NULL, NULL, NULL, NULL, NULL);
