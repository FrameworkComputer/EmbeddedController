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
