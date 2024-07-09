/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Queue data structure.
 */
#ifndef __CROS_EC_QUEUE_H
#define __CROS_EC_QUEUE_H

#include "common.h"
#include "util.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

	size_t buffer_units; /* size of buffer (in units) */
	size_t buffer_units_mask; /* size of buffer (in units) - 1*/
	size_t unit_bytes; /* size of unit   (in byte) */
	uint8_t *buffer;
};

/*
 * Convenience macro for construction of a Queue along with its backing buffer
 * and state structure.  This macro creates a compound literal that can be used
 * to statically initialize a queue.
 */
#define QUEUE(SIZE, TYPE, POLICY)                                             \
	((struct queue){                                                      \
		.state = &((struct queue_state){}),                           \
		.policy = &POLICY,                                            \
		.buffer_units = BUILD_CHECK_INLINE(SIZE, POWER_OF_TWO(SIZE)), \
		.buffer_units_mask = SIZE - 1,                                \
		.unit_bytes = sizeof(TYPE),                                   \
		.buffer = (uint8_t *)&((TYPE[SIZE]){}),                       \
	})

/* Initialize the queue to empty state. */
void queue_init(struct queue const *q);

/* Return TRUE if the queue is empty. */
int queue_is_empty(struct queue const *q);

/* Return the number of units stored in the queue. */
size_t queue_count(struct queue const *q);

/* Return the number of units worth of free space the queue has. */
size_t queue_space(struct queue const *q);

/* Return TRUE if the queue is full. */
int queue_is_full(struct queue const *q);

struct queue_iterator_state {
	size_t offset;
	size_t head;
	size_t tail;
};

struct queue_iterator {
	void *ptr;
	struct queue_iterator_state _state;
};

/**
 * Get a pointer to the first element (the head).
 *
 * @param q Pointer to the queue.
 * @param it Pointer to an Iterator that will point to the first element.
 */
void queue_begin(struct queue const *q, struct queue_iterator *it);

/**
 * Get a pointer to the next element in the queue given a current iterator.
 *
 * @param q Pointer to a constant queue to query.
 * @param it The iterator to move forward.
 */
void queue_next(struct queue const *q, struct queue_iterator *it);

/*
 * Chunk based queue access.  A queue_chunk is a contiguous region of queue
 * buffer units.
 */
struct queue_chunk {
	size_t count;
	void *buffer;
};

/*
 * Return the largest contiguous block of free space from the tail of the
 * queue + offset.  This may not be all of the available free space in the
 * queue.  Once some or all of the free space has been written to you must call
 * queue_advance_tail to update the queue.  You do not need to fill all of the
 * free space returned before calling queue_advance_tail, and you may call
 * queue_advance tail multiple times for a single chunk.  But you must not
 * advance the tail more than the length of the chunk, or more than the actual
 * number of units that you have written to the free space represented by the
 * chunk.
 *
 * Since this function returns continuous space, offset will allow accessing
 * writable memory that is wrapped. For example:
 *
 *  *: Used entry
 *  -: Free space
 *
 *    H  T
 * |--***---|
 *
 * A call to queue_get_write_chunk(&q, 0) will return 3 entries starting at T.
 * To be able to also write the 2 leading writable indicies, we'll need to use
 * queue_get_write_chunk(&q, 3), which will return 2 entries at queue index 0.
 */
struct queue_chunk queue_get_write_chunk(struct queue const *q, size_t offset);

/*
 * Return the largest contiguous block of units from the head of the queue.
 * This may not be all of the available units in the queue.  Similar rules to
 * the above apply to reading from this chunk, you can call queue_advance_head
 * after reading, and you can all it multiple times if you like.  However, if
 * you do not call queue_advance_head this chunk will effectively be a peek
 * into the queue contents, and later calls to queue_remove_* will see the
 * same units.
 */
struct queue_chunk queue_get_read_chunk(struct queue const *q);

/*
 * Move the queue head pointer forward count units.  This discards count
 * elements from the head of the queue.  It will only discard up to the total
 * number of elements in the queue, and it returns the number discarded.
 */
size_t queue_advance_head(struct queue const *q, size_t count);

/*
 * Move the queue tail pointer forward count units.  This signals to the queue
 * that count new elements have been added to the queue using a queue_chunk
 * that was returned by queue_get_write_chunk.  Make sure that count units have
 * been added to the chunk before calling queue_advance_tail.
 */
size_t queue_advance_tail(struct queue const *q, size_t count);

/* Add one unit to queue. */
size_t queue_add_unit(struct queue const *q, const void *src);

/* Add multiple units to queue. */
size_t queue_add_units(struct queue const *q, const void *src, size_t count);

/* Add multiple units to queue using supplied memcpy. */
size_t queue_add_memcpy(struct queue const *q, const void *src, size_t count,
			void *(*memcpy)(void *dest, const void *src, size_t n));

/* Remove one unit from the begin of the queue. */
size_t queue_remove_unit(struct queue const *q, void *dest);

/* Remove multiple units from the begin of the queue. */
size_t queue_remove_units(struct queue const *q, void *dest, size_t count);

/* Remove multiple units from the begin of the queue using supplied memcpy. */
size_t queue_remove_memcpy(struct queue const *q, void *dest, size_t count,
			   void *(*memcpy)(void *dest, const void *src,
					   size_t n));

/* Peek (return but don't remove) the count elements starting with the i'th. */
size_t queue_peek_units(struct queue const *q, void *dest, size_t i,
			size_t count);

/* Peek (return but don't remove) the count elements starting with the i'th. */
size_t
queue_peek_memcpy(struct queue const *q, void *dest, size_t i, size_t count,
		  void *(*memcpy)(void *dest, const void *src, size_t n));

/*
 * These macros will statically select the queue functions based on the number
 * of units that are to be added or removed if they can.  The single unit add
 * and remove functions are much faster than calling the equivalent generic
 * version with a count of one.
 */
#define QUEUE_ADD_UNITS(q, src, count)                           \
	({                                                       \
		size_t result;                                   \
                                                                 \
		if (count == 1)                                  \
			result = queue_add_unit(q, src);         \
		else                                             \
			result = queue_add_units(q, src, count); \
                                                                 \
		result;                                          \
	})

#define QUEUE_REMOVE_UNITS(q, dest, count)                           \
	({                                                           \
		size_t result;                                       \
                                                                     \
		if (count == 1)                                      \
			result = queue_remove_unit(q, dest);         \
		else                                                 \
			result = queue_remove_units(q, dest, count); \
                                                                     \
		result;                                              \
	})

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_QUEUE_H */
