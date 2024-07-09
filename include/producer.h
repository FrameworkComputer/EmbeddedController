/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Producer interface
 *
 * The producer abstraction allows for code that wants to write into a queue and
 * be notified when the queue is read from so that it can take action, such as
 * adding new units to the queue.
 */
#ifndef __CROS_EC_PRODUCER_H
#define __CROS_EC_PRODUCER_H

#include "queue.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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
	 * A producer references the queue that it is writing into.
	 */
	struct queue const *queue;

	struct producer_ops const *ops;
};

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_PRODUCER_H */
