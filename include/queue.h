/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Queue data structure.
 */
#ifndef INCLUDE_QUEUE_H
#define INCLUDE_QUEUE_H

#include "common.h"

#include <stddef.h>
#include <stdint.h>

/* Generic queue container. */

/*
 * RAM state for a queue.
 */
struct queue_state {
	/*
	 * The queue head and tail pointers are not wrapped until they are
	 * needed to access the queue buffer.  This has a number of advantages,
	 * the queue doesn't have to waste an entry to disambiguate full and
	 * empty for one.  It also provides a convenient total enqueue/dequeue
	 * log (one that does wrap at the limit of a size_t however).
	 *
	 * Empty:
	 *     head == tail
	 *
	 * Full:
	 *     head - tail == buffer_units
	 */
	size_t head; /* head: next to dequeue */
	size_t tail; /* tail: next to enqueue */
};

/*
 * Queue configuration stored in flash.
 */
struct queue {
	struct queue_state volatile *state;

	size_t  buffer_units; /* size of buffer (in units) */
	size_t  unit_bytes;   /* size of unit   (in byte) */
	uint8_t *buffer;
};

/*
 * Convenience macro for construction of a Queue along with its backing buffer
 * and state structure.
 */
#define QUEUE_CONFIG(NAME, SIZE, TYPE)					\
	static TYPE CONCAT2(NAME, _buffer)[SIZE];			\
									\
	static struct queue_state CONCAT2(NAME, _state);		\
	struct queue const NAME =					\
	{								\
		.state        = &CONCAT2(NAME, _state),			\
		.buffer_units = SIZE,					\
		.unit_bytes   = sizeof(TYPE),				\
		.buffer       = (uint8_t *) CONCAT2(NAME, _buffer),	\
	};

/* Initialize the queue to empty state. */
void queue_init(struct queue const *q);

/* Return TRUE if the queue is empty. */
int queue_is_empty(struct queue const *q);

/* Return the number of units stored in the queue. */
size_t queue_count(struct queue const *q);

/* Return the number of units worth of free space the queue has. */
size_t queue_space(struct queue const *q);

/* Add one unit to queue. */
size_t queue_add_unit(struct queue const *q, void const *src);

/* Add multiple units to queue. */
size_t queue_add_units(struct queue const *q, void const *src, size_t count);

/* Add multiple units to queue using supplied memcpy. */
size_t queue_add_memcpy(struct queue const *q,
			void const *src,
			size_t count,
			void *(*memcpy)(void *dest,
					void const *src,
					size_t n));

/* Remove one unit from the begin of the queue. */
size_t queue_remove_unit(struct queue const *q, void *dest);

/* Remove multiple units from the begin of the queue. */
size_t queue_remove_units(struct queue const *q, void *dest, size_t count);

/* Remove multiple units from the begin of the queue using supplied memcpy. */
size_t queue_remove_memcpy(struct queue const *q,
			   void *dest,
			   size_t count,
			   void *(*memcpy)(void *dest,
					   void const *src,
					   size_t n));

/* Peek (return but don't remove) the count elements starting with the i'th. */
size_t queue_peek_units(struct queue const *q,
			void *dest,
			size_t i,
			size_t count);

/*
 * These macros will statically select the queue functions based on the number
 * of units that are to be added or removed if they can.  The single unit add
 * and remove functions are much faster than calling the equivalent generic
 * version with a count of one.
 */
#define QUEUE_ADD_UNITS(q, src, count)					\
	({								\
		size_t result;						\
									\
		if (count == 1)						\
			result = queue_add_unit(q, src);		\
		else							\
			result = queue_add_units(q, src, count);	\
									\
		result;							\
	})

#define QUEUE_REMOVE_UNITS(q, dest, count)				\
	({								\
		size_t result;						\
									\
		if (count == 1)						\
			result = queue_remove_unit(q, dest);		\
		else							\
			result = queue_remove_units(q, dest, count);	\
									\
		result;							\
	})

#endif /* INCLUDE_QUEUE_H */
