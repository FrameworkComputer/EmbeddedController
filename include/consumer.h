/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Consumer interface
 *
 * The consumer abstraction allows for code that wants to be able to read from
 * a queue, and be notified of new additions to the queue, or of requests to
 * flush (empty) the queue.
 */
#ifndef __CROS_EC_CONSUMER_H
#define __CROS_EC_CONSUMER_H

#include "queue.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct consumer;
struct producer;

struct consumer_ops {
	/*
	 * Inform the consumer that count units were written to the queue.
	 * This gives it the oportunity to read additional units from the queue
	 * or to wake up a task or interrupt to do the same.  If a consumer has
	 * no need for this information it can set this to NULL.
	 *
	 * If count is zero, the call serves as a request to "flush" any
	 * previously queued data, that is consume the data without further
	 * delay.  Some consumers may not know about the flushing semantics,
	 * they will ignore a notification with zero length, and they will
	 * eagerly process any data as it is written, which is allowed.
	 *
	 * Some producers may not know about flushing semantics, and care has to
	 * be taken that a consumer does not wait infinitely for a flush request
	 * that never comes.  Consumers must check for QUEUE_BUFFERED_MODE in
	 * queue->state->flags, and eagerly process any data if not set.
	 *
	 * When QUEUE_BUFFERED_MODE is set, the consumer is allowed to not react
	 * immediately to calls to written() with non-zero counts, and instead
	 * wait until a sufficient amount of data has been accumulated for
	 * efficient processing (or until flush is explicitly requested).  In
	 * particular, this is used in usb-stream.c for filling USB packets up
	 * to 64 bytes, even when data is enqueued in smaller chunks.
	 */
	void (*written)(struct consumer const *consumer, size_t count);
};

struct consumer {
	/*
	 * A consumer references the queue that it is reading from.
	 */
	struct queue const *queue;

	struct consumer_ops const *ops;
};

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_CONSUMER_H */
