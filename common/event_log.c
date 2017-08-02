/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "event_log.h"
#include "hooks.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Event log FIFO */
#define UNIT_SIZE sizeof(struct event_log_entry)
#define LOG_SIZE (CONFIG_EVENT_LOG_SIZE/UNIT_SIZE)
static struct event_log_entry __bss_slow log_events[LOG_SIZE];
BUILD_ASSERT(POWER_OF_TWO(LOG_SIZE));

/*
 * The FIFO pointers are defined as following :
 * "log_head" is the next available event to dequeue.
 * "log_tail" is marking the end of the FIFO content (after last committed
 *  event)
 * "log_tail_next" is the next available spot to enqueue events.
 * The pointers are not wrapped until they are used, so we don't need an extra
 * entry to disambiguate between full and empty FIFO.
 *
 * For concurrency, several tasks might try to enqueue events in parallel with
 * log_add_event(). Only one task is dequeuing events (host commands, VDM,
 * TPM command handler). When the FIFO is full, log_add_event() will discard
 * the oldest events, so "log_head" is incremented/decremented in a critical
 * section since it is accessed from both log_add_event() and
 * log_dequeue_event(). log_tail_next is also protected as several writers can
 * race to add an event to the queue.
 * When a writer is done adding its event, it is updating log_tail,
 * so the event can be consumed by log_dequeue_event().
 */
static size_t log_head;
static size_t log_tail;
static size_t log_tail_next;

/* Size of one FIFO entry */
#define ENTRY_SIZE(payload_sz) (1+DIV_ROUND_UP((payload_sz), UNIT_SIZE))

void log_add_event(uint8_t type, uint8_t size, uint16_t data,
			  void *payload, uint32_t timestamp)
{
	struct event_log_entry *r;
	size_t payload_size = EVENT_LOG_SIZE(size);
	size_t total_size = ENTRY_SIZE(payload_size);
	size_t current_tail, first;

	/* --- critical section : reserve queue space --- */
	interrupt_disable();
	current_tail = log_tail_next;
	log_tail_next = current_tail + total_size;
	interrupt_enable();
	/* --- end of critical section --- */

	/* Out of space : discard the oldest entry */
	while ((LOG_SIZE - (current_tail - log_head)) < total_size) {
		struct event_log_entry *oldest;
		/* --- critical section : atomically free-up space --- */
		interrupt_disable();
		oldest = log_events + (log_head & (LOG_SIZE - 1));
		log_head += ENTRY_SIZE(EVENT_LOG_SIZE(oldest->size));
		interrupt_enable();
		/* --- end of critical section --- */
	}

	r = log_events + (current_tail & (LOG_SIZE - 1));

	r->timestamp = timestamp;
	r->type = type;
	r->size = size;
	r->data = data;
	/* copy the payload into the FIFO */
	first = MIN(total_size - 1, (LOG_SIZE -
		    (current_tail & (LOG_SIZE - 1))) - 1);
	if (first)
		memcpy(r->payload, payload, first * UNIT_SIZE);
	if (first < total_size - 1)
		memcpy(log_events, ((uint8_t *)payload) + first * UNIT_SIZE,
			(total_size - first) * UNIT_SIZE);
	/* mark the entry available in the queue if nobody is behind us */
	if (current_tail == log_tail)
		log_tail = log_tail_next;
}

int log_dequeue_event(struct event_log_entry *r)
{
	uint32_t now = get_time().val >> EVENT_LOG_TIMESTAMP_SHIFT;
	unsigned int total_size, first;
	struct event_log_entry *entry;
	size_t current_head;

retry:
	current_head = log_head;
	/* The log FIFO is empty */
	if (log_tail == current_head) {
		memset(r, 0, UNIT_SIZE);
		r->type = EVENT_LOG_NO_ENTRY;
		return UNIT_SIZE;
	}

	entry = log_events + (current_head & (LOG_SIZE - 1));
	total_size = ENTRY_SIZE(EVENT_LOG_SIZE(entry->size));
	first = MIN(total_size, LOG_SIZE - (current_head & (LOG_SIZE - 1)));
	memcpy(r, entry, first * UNIT_SIZE);
	if (first < total_size)
		memcpy(r + first, log_events, (total_size-first) * UNIT_SIZE);

	/* --- critical section : remove the entry from the queue --- */
	interrupt_disable();
	if (log_head != current_head) { /* our entry was thrown away */
		interrupt_enable();
		goto retry;
	}
	log_head += total_size;
	interrupt_enable();
	/* --- end of critical section --- */

	/* fixup the timestamp : number of milliseconds in the past */
	r->timestamp = now - r->timestamp;

	return total_size * UNIT_SIZE;
}
