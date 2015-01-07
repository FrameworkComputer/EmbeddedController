/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Queue data structure implementation.
 */
#include "queue.h"
#include "util.h"

void queue_init(struct queue const *q)
{
	ASSERT(POWER_OF_TWO(q->buffer_units));

	q->state->head = 0;
	q->state->tail = 0;
}

int queue_is_empty(struct queue const *q)
{
	return q->state->head == q->state->tail;
}

size_t queue_count(struct queue const *q)
{
	return q->state->tail - q->state->head;
}

size_t queue_space(struct queue const *q)
{
	return q->buffer_units - queue_count(q);
}

size_t queue_add_unit(struct queue const *q, void const *src)
{
	size_t tail = q->state->tail & (q->buffer_units - 1);

	if (queue_space(q) == 0)
		return 0;

	if (q->unit_bytes == 1)
		q->buffer[tail] = *((uint8_t *) src);
	else
		memcpy(q->buffer + tail * q->unit_bytes, src, q->unit_bytes);

	q->state->tail += 1;

	return 1;
}

size_t queue_add_units(struct queue const *q, void const *src, size_t count)
{
	return queue_add_memcpy(q, src, count, memcpy);
}

size_t queue_add_memcpy(struct queue const *q,
			void const *src,
			size_t count,
			void *(*memcpy)(void *dest,
					void const *src,
					size_t n))
{
	size_t transfer = MIN(count, queue_space(q));
	size_t tail     = q->state->tail & (q->buffer_units - 1);
	size_t first    = MIN(transfer, q->buffer_units - tail);

	memcpy(q->buffer + tail * q->unit_bytes,
	       src,
	       first * q->unit_bytes);

	if (first < transfer)
		memcpy(q->buffer,
		       ((uint8_t const *) src) + first * q->unit_bytes,
		       (transfer - first) * q->unit_bytes);

	q->state->tail += transfer;

	return transfer;
}

static void queue_read_safe(struct queue const *q,
			    void *dest,
			    size_t head,
			    size_t transfer,
			    void *(*memcpy)(void *dest,
					    void const *src,
					    size_t n))
{
	size_t first = MIN(transfer, q->buffer_units - head);

	memcpy(dest,
	       q->buffer + head * q->unit_bytes,
	       first * q->unit_bytes);

	if (first < transfer)
		memcpy(((uint8_t *) dest) + first * q->unit_bytes,
		       q->buffer,
		       (transfer - first) * q->unit_bytes);
}

size_t queue_remove_unit(struct queue const *q, void *dest)
{
	size_t head = q->state->head & (q->buffer_units - 1);

	if (queue_count(q) == 0)
		return 0;

	if (q->unit_bytes == 1)
		*((uint8_t *) dest) = q->buffer[head];
	else
		memcpy(dest, q->buffer + head * q->unit_bytes, q->unit_bytes);

	q->state->head += 1;

	return 1;
}

size_t queue_remove_units(struct queue const *q, void *dest, size_t count)
{
	return queue_remove_memcpy(q, dest, count, memcpy);
}

size_t queue_remove_memcpy(struct queue const *q,
			   void *dest,
			   size_t count,
			   void *(*memcpy)(void *dest,
					   void const *src,
					   size_t n))
{
	size_t transfer = MIN(count, queue_count(q));
	size_t head     = q->state->head & (q->buffer_units - 1);

	queue_read_safe(q, dest, head, transfer, memcpy);

	q->state->head += transfer;

	return transfer;
}

size_t queue_peek_units(struct queue const *q,
			void *dest,
			size_t i,
			size_t count)
{
	size_t available = queue_count(q);
	size_t transfer  = MIN(count, available - i);

	if (i < available) {
		size_t head = (q->state->head + i) & (q->buffer_units - 1);

		queue_read_safe(q, dest, head, transfer, memcpy);
	}

	return transfer;
}
