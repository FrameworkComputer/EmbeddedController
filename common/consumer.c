/* Copyright (c) 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Consumer methods
 */
#include "consumer.h"
#include "producer.h"

void consumer_notify_directly(struct consumer const *consumer, size_t count)
{
	if (count && consumer->ops->written)
		consumer->ops->written(consumer, count);
}

size_t consumer_read_unit(struct consumer const *consumer, void *unit)
{
	size_t removed = queue_remove_unit(consumer->queue, unit);

	producer_notify_directly(consumer->producer, removed);

	return removed;
}

size_t consumer_read_memcpy(struct consumer const *consumer,
			    void *units,
			    size_t count,
			    void *(*memcpy)(void *dest,
					    void const *src,
					    size_t n))
{
	size_t removed = queue_remove_memcpy(consumer->queue,
					     units,
					     count,
					     memcpy);

	producer_notify_directly(consumer->producer, removed);

	return removed;
}
