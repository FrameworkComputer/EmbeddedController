/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Queue policies.
 */
#include "queue_policies.h"
#include "util.h"

#include <stddef.h>

void queue_add_direct(struct queue_policy const *policy, size_t count)
{
	struct queue_policy_direct const *direct =
		DOWNCAST(policy, struct queue_policy_direct, policy);

	if (count && direct->consumer->ops->written)
		direct->consumer->ops->written(direct->consumer, count);
}

void queue_remove_direct(struct queue_policy const *policy, size_t count)
{
	struct queue_policy_direct const *direct =
		DOWNCAST(policy, struct queue_policy_direct, policy);

	if (count && direct->producer->ops->read)
		direct->producer->ops->read(direct->producer, count);
}

struct producer const null_producer = {
	.queue = NULL,
	.ops   = &((struct producer_ops const) {
		 .read = NULL,
	}),
};

struct consumer const null_consumer = {
	.queue = NULL,
	.ops   = &((struct consumer_ops const) {
		.written = NULL,
	}),
};
