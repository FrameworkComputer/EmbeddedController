/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Queue data structure.
 */

#include "common.h"

/* Generic queue container.
 *
 *   head: next to dequeqe
 *   tail: next to enqueue
 *
 *   Empty:
 *     head == tail
 *   Full:
 *     ((tail + 1) % buf_bytes) == head
 */
struct queue {
	int head, tail;
	int buf_bytes;   /* size of buffer (in byte) */
	int unit_bytes;  /* size of unit (in byte) */
	uint8_t *buf;
};

/* Reset the queue to empty state. */
void queue_reset(struct queue *queue);

/* Return TRUE if the queue is empty. */
int queue_is_empty(const struct queue *q);

/* Return TRUE if the queue has at least one unit space. */
int queue_has_space(const struct queue *q, int unit_count);

/* Add multiple units into queue. */
void queue_add_units(struct queue *q, const void *src, int unit_count);

/* Remove one unit from the begin of the queue. */
int queue_remove_unit(struct queue *q, void *dest);
