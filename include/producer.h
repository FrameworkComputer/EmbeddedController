/* Copyright (c) 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Producer interface
 *
 * The producer abstraction allows for code that wants to write into a queue and
 * be notified when the queue is read from so that it can take action, such as
 * adding new units to the queue.
 */
#ifndef INCLUDE_PRODUCER_H
#define INCLUDE_PRODUCER_H

#include "queue.h"

#include <stddef.h>
#include <stdint.h>

struct consumer;
struct producer;

struct producer_ops {
	/*
	 * Inform the producer that count units were read from the queue.  This
	 * gives it the oportunity to write additional units to the queue or to
	 * wake up a task or interrupt to do the same.  If a producer has no
	 * need for this information it can set this to NULL.
	 */
	void (*read)(struct producer const *producer, size_t count);
};

struct producer {
	/*
	 * A producer references the consumer at the other end of the queue.
	 * This allows the producer to notify the consumer when new units are
	 * added to the queue.
	 */
	struct consumer const *consumer;

	/*
	 * A producer also references the queue that it is writing into.  This
	 * and the consumer reference above could be more flexibly replaced by
	 * a queue manager object that could handle multiple producer/consumers
	 * or alternate notification mechanisms.  But that complexity is not
	 * yet warranted.
	 */
	struct queue const *queue;

	struct producer_ops const *ops;
};

/*
 * Notify the producer by calling its read method directly, as opposed to from
 * a deferred callback or another task.
 */
void producer_notify_directly(struct producer const *producer, size_t count);

/*
 * Write a single unit to the queue and notify the associated consumer.  Return
 * the number of units written.
 */
size_t producer_write_unit(struct producer const *producer, void const *unit);

/*
 * Write multiple units to the queue, using the provided memcpy like routine
 * and notify the consumer.  Return the number of units written.
 */
size_t producer_write_memcpy(struct producer const *producer,
			     void const *units,
			     size_t count,
			     void *(*memcpy)(void *dest,
					     void const *src,
					     size_t n));

#endif /* INCLUDE_PRODUCER_H */
