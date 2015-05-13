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
 * Queue policies describe how a queue behaves (who it notifies, in what
 * contexts) when units are added or removed from the queue.
 *
 * The queue_policy structure is a table of virtual function pointers.  Each
 * policy will implement the add and remove functions.  Each policy also
 * optionally defines a new structure that contains the queue_policy struct by
 * value any any additional data needed to implement the policy.  This
 * structure is then initialized using the policy specific functions and the
 * additional data.
 *
 * If a policy is so simple that it doesn't require any additional data then
 * the queue_policy structure can just be used directly, as queue_policy_null
 * does below.
 */
struct queue_policy {
	void (*add)(struct queue_policy const *queue_policy, size_t count);
	void (*remove)(struct queue_policy const *queue_policy, size_t count);
};

/*
 * The NULL policy does no notification when units are added or removed from
 * the queue.  Since the NULL policy doesn't do anything it doesn't actually
 * need to extend the queue_policy interface and can just use it directly.
 *
 * The QUEUE_NULL macro constructs a queue that uses the NULL policy.
 */
extern struct queue_policy const queue_policy_null;

#define QUEUE_NULL(SIZE, TYPE) QUEUE(SIZE, TYPE, queue_policy_null)

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

	struct queue_policy const *policy;

	size_t  buffer_units; /* size of buffer (in units) */
	size_t  unit_bytes;   /* size of unit   (in byte) */
	uint8_t *buffer;
};

/*
 * Convenience macro for construction of a Queue along with its backing buffer
 * and state structure.  This macro creates a compound literal that can be used
 * to statically initialize a queue.
 */
#define QUEUE(SIZE, TYPE, POLICY)				\
	((struct queue) {					\
		.state        = &((struct queue_state){}),	\
		.policy       = &POLICY,			\
		.buffer_units = SIZE,				\
		.unit_bytes   = sizeof(TYPE),			\
		.buffer       = (uint8_t *) &((TYPE[SIZE]){}),	\
	})

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
