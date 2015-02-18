/* Copyright (c) 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Producer methods
 */
#include "consumer.h"
#include "producer.h"

void producer_notify_directly(struct producer const *producer, size_t count)
{
	if (count && producer->ops->read)
		producer->ops->read(producer, count);
}

size_t producer_write_unit(struct producer const *producer, void const *unit)
{
	size_t added = queue_add_unit(producer->queue, unit);

	consumer_notify_directly(producer->consumer, added);

	return added;
}

size_t producer_write_memcpy(struct producer const *producer,
			     void const *units,
			     size_t count,
			     void *(*memcpy)(void *dest,
					     void const *src,
					     size_t n))
{
	size_t added = queue_add_memcpy(producer->queue,
					units,
					count,
					memcpy);

	consumer_notify_directly(producer->consumer, added);

	return added;
}
