/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "task.h"
#include "timer.h"
#include "usb_pd.h"
#ifdef HAS_TASK_HOSTCMD
#include "usb_pd_config.h"
#endif
#include "util.h"

/* Event log FIFO */
#define UNIT_SIZE sizeof(struct ec_response_pd_log)
#define LOG_SIZE (CONFIG_USB_PD_LOG_SIZE/UNIT_SIZE)
static struct ec_response_pd_log log_events[LOG_SIZE];
BUILD_ASSERT(POWER_OF_TWO(LOG_SIZE));
/*
 * The FIFO pointers are defined as following :
 * "log_head" is the next available event to dequeue.
 * "log_tail" is marking the end of the FIFO content (after last commited event)
 * "log_tail_next" is the next available spot to enqueue events.
 * The pointers are not wrapped until they are used, so we don't need an extra
 * entry to disambiguate between full and empty FIFO.
 *
 * For concurrency, several tasks might try to enqueue events in parallel with
 * pd_log_event(). Only one task is dequeuing events (host commands or VDM).
 * When the FIFO is full, pd_log_event() will discard the oldest events,
 * so "log_head" is incremented/decremented in a critical section since it is
 * accessed from both pd_log_event() and pd_log_dequeue().
 * log_tail_next is also protected as several writers can race to add an event
 * to the queue.
 * When a writer is done adding its event, it is updating log_tail,
 * so the event can be consumed by pd_log_dequeue().
 */
static size_t log_head;
static size_t log_tail;
static size_t log_tail_next;

/* Size of one FIFO entry */
#define ENTRY_SIZE(payload_sz) (1+DIV_ROUND_UP((payload_sz), UNIT_SIZE))

static void log_add_event(uint8_t type, uint8_t size_port, uint16_t data,
			  void *payload, uint32_t timestamp)
{
	struct ec_response_pd_log *r;
	size_t payload_size = PD_LOG_SIZE(size_port);
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
		struct ec_response_pd_log *oldest;
		/* --- critical section : atomically free-up space --- */
		interrupt_disable();
		oldest = log_events + (log_head & (LOG_SIZE - 1));
		log_head += ENTRY_SIZE(PD_LOG_SIZE(oldest->size_port));
		interrupt_enable();
		/* --- end of critical section --- */
	}

	r = log_events + (current_tail & (LOG_SIZE - 1));

	r->timestamp = timestamp;
	r->type = type;
	r->size_port = size_port;
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

void pd_log_event(uint8_t type, uint8_t size_port,
		  uint16_t data, void *payload)
{
	uint32_t timestamp = get_time().val >> PD_LOG_TIMESTAMP_SHIFT;

	log_add_event(type, size_port, data, payload, timestamp);
}

static int pd_log_dequeue(struct ec_response_pd_log *r)
{
	uint32_t now = get_time().val >> PD_LOG_TIMESTAMP_SHIFT;
	unsigned total_size, first;
	struct ec_response_pd_log *entry;
	size_t current_head;

retry:
	current_head = log_head;
	/* The log FIFO is empty */
	if (log_tail == current_head) {
		memset(r, 0, UNIT_SIZE);
		r->type = PD_EVENT_NO_ENTRY;
		return UNIT_SIZE;
	}

	entry = log_events + (current_head & (LOG_SIZE - 1));
	total_size = ENTRY_SIZE(PD_LOG_SIZE(entry->size_port));
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

#ifdef HAS_TASK_HOSTCMD

/* number of accessory entries we have queued since last check */
static volatile int incoming_logs;

void pd_log_recv_vdm(int port, int cnt, uint32_t *payload)
{
	struct ec_response_pd_log *r = (void *)&payload[1];
	/* update port number from MCU point of view */
	size_t size = PD_LOG_SIZE(r->size_port);
	uint8_t size_port = PD_LOG_PORT_SIZE(size, port);
	uint32_t timestamp;

	if ((cnt < 2 + DIV_ROUND_UP(size, sizeof(uint32_t))) ||
	    !(payload[0] & VDO_SRC_RESPONDER))
		/* Not a proper log entry, bail out */
		return;

	if (r->type != PD_EVENT_NO_ENTRY) {
		timestamp = (get_time().val >> PD_LOG_TIMESTAMP_SHIFT)
			  - r->timestamp;
		log_add_event(r->type, size_port, r->data, r->payload,
			      timestamp);
		/* record that we have enqueued new content */
		incoming_logs++;
	}
}

/* we are a PD MCU/EC, send back the events to the host */
static int hc_pd_get_log_entry(struct host_cmd_handler_args *args)
{
	struct ec_response_pd_log *r = args->response;

dequeue_retry:
	args->response_size = pd_log_dequeue(r);
	/* if the MCU log no longer has entries, try connected accessories */
	if (r->type == PD_EVENT_NO_ENTRY) {
		int i, res;
		incoming_logs = 0;
		for (i = 0; i < PD_PORT_COUNT; ++i) {
			res = pd_fetch_acc_log_entry(i);
			if (res == EC_RES_BUSY) /* host should retry */
				return EC_RES_BUSY;
		}
		/* we have received new entries from an accessory */
		if (incoming_logs)
			goto dequeue_retry;
		/* else the current entry is already "PD_EVENT_NO_ENTRY" */
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PD_GET_LOG_ENTRY,
		     hc_pd_get_log_entry,
		     EC_VER_MASK(0));
#else /* !HAS_TASK_HOSTCMD */
/* we are a PD accessory, send back the events as a VDM (VDO_CMD_GET_LOG) */
int pd_vdm_get_log_entry(uint32_t *payload)
{
	struct ec_response_pd_log *r = (void *)&payload[1];
	int byte_size;

	byte_size = pd_log_dequeue(r);

	return 1 + DIV_ROUND_UP(byte_size, sizeof(uint32_t));
}
#endif /* !HAS_TASK_HOSTCMD */
