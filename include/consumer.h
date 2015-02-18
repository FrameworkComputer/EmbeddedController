/* Copyright (c) 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Consumer interface
 *
 * The consumer abstraction allows for code that wants to be able to read from
 * a queue, and be notified of new additions to the queue, or of requests to
 * flush (empty) the queue.
 */
#ifndef INCLUDE_CONSUMER_H
#define INCLUDE_CONSUMER_H

#include "queue.h"

#include <stddef.h>
#include <stdint.h>

struct consumer;
struct producer;

struct consumer_ops {
	/*
	 * Inform the consumer that count units were written to the queue.
	 * This gives it the oportunity to read additional units from the queue
	 * or to wake up a task or interrupt to do the same.  If a consumer has
	 * no need for this information it can set this to NULL.
	 */
	void (*written)(struct consumer const *consumer, size_t count);

	/*
	 * Flush (read) everything from the associated queue.  This call blocks
	 * until the consumer has flushed the queue.
	 */
	void (*flush)(struct consumer const *consumer);
};

struct consumer {
	/*
	 * A consumer references the producer at the other end of the queue.
	 * This allows the consumer to notify the producer when units are
	 * removed from the queue.
	 */
	struct producer const *producer;

	/*
	 * A consumer also references the queue that it is reading from.  This
	 * and the producer reference above could be more flexibly replaced by
	 * a queue manager object that could handle multiple producer/consumers
	 * or alternate notification mechanisms.  But that complexity is not
	 * yet warranted.
	 */
	struct queue const *queue;

	struct consumer_ops const *ops;
};

/*
 * Notify the consumer by calling its written method directly, as opposed to
 * from a deferred callback or another task.
 */
void consumer_notify_directly(struct consumer const *consumer, size_t count);

/*
 * Read a single unit from the queue and notify the associated producer.
 * Return the number of units read.
 */
size_t consumer_read_unit(struct consumer const *consumer, void *unit);

/*
 * Read multiple units from the queue, using the provided memcpy like routine
 * and notify the producer.  Return the number of units read.
 */
size_t consumer_read_memcpy(struct consumer const *consumer,
			    void *units,
			    size_t count,
			    void *(*memcpy)(void *dest,
					    void const *src,
					    size_t n));

#endif /* INCLUDE_CONSUMER_H */
