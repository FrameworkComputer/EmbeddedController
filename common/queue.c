/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Queue data structure implementation.
 */
#include "console.h"
#include "queue.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_MOTION_SENSE, format, ## args)

static void queue_action_null(struct queue_policy const *policy, size_t count)
{
}

struct queue_policy const queue_policy_null = {
	.add    = queue_action_null,
	.remove = queue_action_null,
};

void queue_init(struct queue const *q)
{
	ASSERT(q->policy);
	ASSERT(q->policy->add);
	ASSERT(q->policy->remove);

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

int queue_is_full(struct queue const *q)
{
	return (queue_space(q) == 0);
}

/*
 * These pictures make the logic below clearer.  The H and T markers are the
 * head and tail indicies after they have been modded by the queue size.  The
 * Empty and Full states are disambiguated by looking at the pre-modded
 * indicies.
 *
 * Empty:       T
 * T == H       H
 *          |----------------|
 *
 * Normal:      H     T
 * H < T    |---******-------|
 *
 * Wrapped:     T         H
 * T < H    |***----------***|
 *
 * Full:        T
 * T == H       H
 *          |****************|
 */

struct queue_chunk queue_get_write_chunk(struct queue const *q, size_t offset)
{
	size_t head = q->state->head & q->buffer_units_mask;
	size_t tail = (q->state->tail + offset) & q->buffer_units_mask;
	size_t last = (tail < head) ? head :   /* Wrapped        */
			q->buffer_units;       /* Normal | Empty */

	/* Make sure that the offset doesn't exceed free space. */
	if (queue_space(q) <= offset)
		return ((struct queue_chunk) {
			.count = 0,
			.buffer = NULL,
		});

	return ((struct queue_chunk) {
		.count = last - tail,
		.buffer = q->buffer + (tail * q->unit_bytes),
	});
}

struct queue_chunk queue_get_read_chunk(struct queue const *q)
{
	size_t head = q->state->head & q->buffer_units_mask;
	size_t tail = q->state->tail & q->buffer_units_mask;
	size_t last = (queue_is_empty(q) ? head : /* Empty          */
		       ((head < tail) ? tail :    /* Normal         */
			q->buffer_units));        /* Wrapped | Full */

	return ((struct queue_chunk) {
		.count = (last - head),
		.buffer = q->buffer + (head * q->unit_bytes),
	});
}

size_t queue_advance_head(struct queue const *q, size_t count)
{
	size_t transfer = MIN(count, queue_count(q));

	q->state->head += transfer;

	q->policy->remove(q->policy, transfer);

	return transfer;
}

size_t queue_advance_tail(struct queue const *q, size_t count)
{
	size_t transfer = MIN(count, queue_space(q));

	q->state->tail += transfer;

	q->policy->add(q->policy, transfer);

	return transfer;
}

size_t queue_add_unit(struct queue const *q, const void *src)
{
	size_t tail = q->state->tail & q->buffer_units_mask;

	if (queue_space(q) == 0)
		return 0;

	if (q->unit_bytes == 1)
		q->buffer[tail] = *((uint8_t *) src);
	else
		memcpy(q->buffer + tail * q->unit_bytes, src, q->unit_bytes);

	return queue_advance_tail(q, 1);
}

size_t queue_add_units(struct queue const *q, const void *src, size_t count)
{
	return queue_add_memcpy(q, src, count, memcpy);
}

size_t queue_add_memcpy(struct queue const *q,
			const void *src,
			size_t count,
			void *(*memcpy)(void *dest,
					const void *src,
					size_t n))
{
	size_t transfer = MIN(count, queue_space(q));
	size_t tail     = q->state->tail & q->buffer_units_mask;
	size_t first    = MIN(transfer, q->buffer_units - tail);

	memcpy(q->buffer + tail * q->unit_bytes,
	       src,
	       first * q->unit_bytes);

	if (first < transfer)
		memcpy(q->buffer,
		       ((uint8_t const *) src) + first * q->unit_bytes,
		       (transfer - first) * q->unit_bytes);

	return queue_advance_tail(q, transfer);
}

static void queue_read_safe(struct queue const *q,
			    void *dest,
			    size_t head,
			    size_t transfer,
			    void *(*memcpy)(void *dest,
					    const void *src,
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
	size_t head = q->state->head & q->buffer_units_mask;

	if (queue_count(q) == 0)
		return 0;

	if (q->unit_bytes == 1)
		*((uint8_t *) dest) = q->buffer[head];
	else
		memcpy(dest, q->buffer + head * q->unit_bytes, q->unit_bytes);

	return queue_advance_head(q, 1);
}

size_t queue_remove_units(struct queue const *q, void *dest, size_t count)
{
	return queue_remove_memcpy(q, dest, count, memcpy);
}

size_t queue_remove_memcpy(struct queue const *q,
			   void *dest,
			   size_t count,
			   void *(*memcpy)(void *dest,
					   const void *src,
					   size_t n))
{
	size_t transfer = MIN(count, queue_count(q));
	size_t head     = q->state->head & q->buffer_units_mask;

	queue_read_safe(q, dest, head, transfer, memcpy);

	return queue_advance_head(q, transfer);
}

size_t queue_peek_units(struct queue const *q,
			void *dest,
			size_t i,
			size_t count)
{
	return queue_peek_memcpy(q, dest, i, count, memcpy);
}

size_t queue_peek_memcpy(struct queue const *q,
			 void *dest,
			 size_t i,
			 size_t count,
			 void *(*memcpy)(void *dest,
				const void *src,
				size_t n))
{
	size_t available = queue_count(q);
	size_t transfer  = MIN(count, available - i);

	if (i < available) {
		size_t head = (q->state->head + i) & q->buffer_units_mask;

		queue_read_safe(q, dest, head, transfer, memcpy);
	}

	return transfer;
}

void queue_begin(struct queue const *q, struct queue_iterator *it)
{
	if (queue_is_empty(q))
		it->ptr = NULL;
	else
		it->ptr = q->buffer + (q->state->head & q->buffer_units_mask) *
			q->unit_bytes;
	it->_state.offset = 0;
	it->_state.head = q->state->head;
	it->_state.tail = q->state->tail;
}

void queue_next(struct queue const *q, struct queue_iterator *it)
{
	uint8_t *ptr = (uint8_t *)it->ptr;

	/* Check if anything changed since the iterator was created. */
	if (it->_state.head != q->state->head ||
	    it->_state.tail != q->state->tail) {
		CPRINTS("Concurrent modification error, queue has changed while"
			" iterating. The iterator is now invalid.");
		it->ptr = NULL;
		return;
	}

	/* Check if iterator is already at end. */
	if (ptr == NULL ||
	    it->_state.head + it->_state.offset == it->_state.tail)
		return;

	it->_state.offset++;
	/* Check if we've reached the end. */
	if (it->_state.head + it->_state.offset == it->_state.tail) {
		it->ptr = NULL;
		return;
	}

	ptr = q->buffer +
	      (((it->_state.head + it->_state.offset) & q->buffer_units_mask) *
	       q->unit_bytes);
	it->ptr = (void *)ptr;
}
