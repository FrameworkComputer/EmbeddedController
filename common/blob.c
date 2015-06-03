/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Handle an opaque blob of data */

#include "blob.h"
#include "common.h"
#include "console.h"
#include "printf.h"
#include "queue.h"
#include "task.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)

#define INCOMING_QUEUE_SIZE 100
#define OUTGOING_QUEUE_SIZE 100


static void incoming_add(struct queue_policy const *queue_policy, size_t count)
{
	task_wake(TASK_ID_BLOB);
}

static void incoming_remove(struct queue_policy const *queue_policy,
			    size_t count)
{
	blob_is_ready_for_more_bytes();
}

static struct queue_policy const incoming_policy = {
	.add = incoming_add,
	.remove = incoming_remove,
};

static void outgoing_add(struct queue_policy const *queue_policy, size_t count)
{
	blob_is_ready_to_emit_bytes();
}

static void outgoing_remove(struct queue_policy const *queue_policy,
			    size_t count)
{
	/* we don't care */
}

static struct queue_policy const outgoing_policy = {
	.add = outgoing_add,
	.remove = outgoing_remove,
};

static struct queue const incoming_q = QUEUE(INCOMING_QUEUE_SIZE, uint8_t,
					     incoming_policy);

static struct queue const outgoing_q = QUEUE(OUTGOING_QUEUE_SIZE, uint8_t,
					     outgoing_policy);


/* Call this to send data to the blob-handler */
size_t put_bytes_to_blob(uint8_t *buffer, size_t count)
{
	return QUEUE_ADD_UNITS(&incoming_q, buffer, count);
}

/* Call this to get data back fom the blob-handler */
size_t get_bytes_from_blob(uint8_t *buffer, size_t count)
{
	return QUEUE_REMOVE_UNITS(&outgoing_q, buffer, count);
}

#define WEAK_FUNC(FOO)							\
	void __ ## FOO(void) {}						\
	void FOO(void)							\
		__attribute__((weak, alias(STRINGIFY(CONCAT2(__, FOO)))))

/* Default callbacks for outsiders */
WEAK_FUNC(blob_is_ready_for_more_bytes);
WEAK_FUNC(blob_is_ready_to_emit_bytes);

/* Do the magic */
void blob_task(void)
{
	static uint8_t buf[INCOMING_QUEUE_SIZE];
	size_t count, i;
	task_id_t me = task_get_current();

	while (1) {
		CPRINTS("task %d waiting for events...", me);
		task_wait_event(-1);
		CPRINTS("task %d awakened!", me);

		count = QUEUE_REMOVE_UNITS(&incoming_q, buf, sizeof(buf));

		CPRINTS("task %d gets: count=%d buf=((%s))", me, count, buf);

		/*
		 * Just to have something to test to begin with, we'll
		 * implement "tr a-zA-Z A-Za-z" and return the result.
		 */
		for (i = 0; i < count; i++) {
			char tmp = buf[i];
			if (tmp >= 'a' && tmp <= 'z')
				buf[i] = tmp - ('a' - 'A');
			else if (tmp >= 'A' && tmp <= 'Z')
				buf[i] = tmp + ('a' - 'A');
		}

		count = QUEUE_ADD_UNITS(&outgoing_q, buf, count);
		CPRINTS("task %d puts: count=%d buf=((%s))", me, buf);
	}
}
