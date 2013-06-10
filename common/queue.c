/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Queue data structure implementation.
 */
#include "queue.h"
#include "util.h"

void queue_reset(struct queue *queue)
{
	queue->head = queue->tail = 0;
}

int queue_is_empty(const struct queue *q)
{
	return q->head == q->tail;
}

int queue_has_space(const struct queue *q, int unit_count)
{
	if (q->tail >= q->head)
		return (q->tail + unit_count * q->unit_bytes) <=
		       (q->head + q->buf_bytes - 1);
	else
		return (q->tail + unit_count * q->unit_bytes) <=
		       (q->head - 1);
}

void queue_add_units(struct queue *q, const void *src, int unit_count)
{
	const uint8_t *s = (const uint8_t *)src;

	if (!queue_has_space(q, unit_count))
		return;

	for (unit_count *= q->unit_bytes; unit_count; unit_count--) {
		q->buf[q->tail++] = *(s++);
		q->tail %= q->buf_bytes;
	}
}

int queue_remove_unit(struct queue *q, void *dest)
{
	int count;
	uint8_t *d = (uint8_t *)dest;

	if (queue_is_empty(q))
		return 0;

	for (count = q->unit_bytes; count; count--) {
		*(d++) = q->buf[q->head++];
		q->head %= q->buf_bytes;
	}

	return 1;
}
