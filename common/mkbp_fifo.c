/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Matrix KeyBoard Protocol FIFO buffer implementation
 */

#include "atomic.h"
#include "common.h"
#include "keyboard_config.h"
#include "mkbp_event.h"
#include "mkbp_fifo.h"
#include "system.h"
#include "task.h"
#include "util.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_KEYBOARD, format, ##args)

/*
 * Common FIFO depth.  This needs to be big enough not to overflow if a
 * series of keys is pressed in rapid succession and the kernel is too busy
 * to read them out right away.
 *
 * RAM usage is (depth * #cols); A 16-entry FIFO will consume 16x16=256 bytes,
 * which is non-trivial but not horrible.
 */

static uint32_t fifo_start; /* first entry */
static uint32_t fifo_end; /* last entry */
static atomic_t fifo_entries; /* number of existing entries */
static uint8_t fifo_max_depth = FIFO_DEPTH;
static struct ec_response_get_next_event_v1 fifo[FIFO_DEPTH];

#ifdef CONFIG_KEYBOARD_PROTOCOL_MKBP
/* Check the FIFO size from the keyboard perspective. */
BUILD_ASSERT(sizeof(fifo[0].data) >= KEYBOARD_COLS_MAX);
#endif

/*
 * Mutex for critical sections of mkbp_fifo_add(), which is called
 * from various tasks.
 */
static K_MUTEX_DEFINE(fifo_add_mutex);
/*
 * Mutex for critical sections of fifo_remove(), which is called from the
 * hostcmd task and from keyboard_clear_buffer().
 */
static K_MUTEX_DEFINE(fifo_remove_mutex);

static int get_data_size(enum ec_mkbp_event e)
{
	switch (e) {
	case EC_MKBP_EVENT_KEY_MATRIX:
		return KEYBOARD_COLS_MAX;

	case EC_MKBP_EVENT_HOST_EVENT64:
		return sizeof(uint64_t);

	case EC_MKBP_EVENT_HOST_EVENT:
	case EC_MKBP_EVENT_BUTTON:
	case EC_MKBP_EVENT_SWITCH:
	case EC_MKBP_EVENT_SYSRQ:
		return sizeof(uint32_t);
	default:
		/* For unknown types, say it's 0. */
		return 0;
	}
}

/**
 * Pop MKBP event data from FIFO
 *
 * @return EC_SUCCESS if entry popped, EC_ERROR_UNKNOWN if FIFO is empty
 */
static int fifo_remove(uint8_t *buffp)
{
	int size;

	mutex_lock(&fifo_remove_mutex);
	if (!fifo_entries) {
		/* no entry remaining in FIFO : return last known state */
		int last = (fifo_start + FIFO_DEPTH - 1) % FIFO_DEPTH;

		size = get_data_size(fifo[last].event_type);

		memcpy(buffp, &fifo[last].data, size);
		mutex_unlock(&fifo_remove_mutex);

		/*
		 * Bail out without changing any FIFO indices and let the
		 * caller know something strange happened. The buffer will
		 * will contain the last known state of the keyboard.
		 */
		return EC_ERROR_UNKNOWN;
	}

	/* Return just the event data. */
	if (buffp) {
		size = get_data_size(fifo[fifo_start].event_type);
		/* skip over event_type. */
		memcpy(buffp, &fifo[fifo_start].data, size);
	}

	fifo_start = (fifo_start + 1) % FIFO_DEPTH;
	atomic_sub(&fifo_entries, 1);
	mutex_unlock(&fifo_remove_mutex);

	return EC_SUCCESS;
}

/*****************************************************************************/
/* Interface */

void mkbp_fifo_depth_update(uint8_t new_max_depth)
{
	fifo_max_depth = new_max_depth;
}

void mkbp_fifo_clear_keyboard(void)
{
	int i, new_fifo_entries = 0;

	CPRINTS("clear keyboard MKBP fifo");

	/*
	 * Order of these locks is important to prevent deadlock since
	 * mkbp_fifo_add() may call fifo_remove().
	 */
	mutex_lock(&fifo_add_mutex);
	mutex_lock(&fifo_remove_mutex);

	/* Reset the end position */
	fifo_end = fifo_start;

	for (i = 0; i < fifo_entries; i++) {
		int cur = (fifo_start + i) % FIFO_DEPTH;

		/* Drop keyboard events */
		if (fifo[cur].event_type == EC_MKBP_EVENT_KEY_MATRIX)
			continue;

		/* And move other events to the front */
		memmove(&fifo[fifo_end], &fifo[cur], sizeof(fifo[cur]));
		fifo_end = (fifo_end + 1) % FIFO_DEPTH;
		++new_fifo_entries;
	}
	fifo_entries = new_fifo_entries;

	mutex_unlock(&fifo_remove_mutex);
	mutex_unlock(&fifo_add_mutex);
}

void mkbp_clear_fifo(void)
{
	int i;

	CPRINTS("clear MKBP fifo");

	/*
	 * Order of these locks is important to prevent deadlock since
	 * mkbp_fifo_add() may call fifo_remove().
	 */
	mutex_lock(&fifo_add_mutex);
	mutex_lock(&fifo_remove_mutex);

	fifo_start = 0;
	fifo_end = 0;
	/* This assignment is safe since both mutexes are held. */
	fifo_entries = 0;
	for (i = 0; i < FIFO_DEPTH; i++)
		memset(&fifo[i], 0, sizeof(struct ec_response_get_next_event));

	mutex_unlock(&fifo_remove_mutex);
	mutex_unlock(&fifo_add_mutex);
}

test_mockable int mkbp_fifo_add(uint8_t event_type, const uint8_t *buffp)
{
	uint8_t size;

	mutex_lock(&fifo_add_mutex);
	if (fifo_entries >= fifo_max_depth) {
		mutex_unlock(&fifo_add_mutex);
		CPRINTS("MKBP common FIFO depth %d reached", fifo_max_depth);

		return EC_ERROR_OVERFLOW;
	}

	size = get_data_size(event_type);
	fifo[fifo_end].event_type = event_type;
	memcpy(&fifo[fifo_end].data, buffp, size);
	fifo_end = (fifo_end + 1) % FIFO_DEPTH;
	atomic_add(&fifo_entries, 1);

	/*
	 * If our event didn't generate an interrupt then the host is still
	 * asleep. In this case, we don't want to queue our event, except if
	 * another event just woke the host (and wake is already in progress).
	 */
	if (!mkbp_send_event(event_type) && fifo_entries == 1)
		fifo_remove(NULL);

	mutex_unlock(&fifo_add_mutex);
	return EC_SUCCESS;
}

int mkbp_fifo_get_next_event(uint8_t *out, enum ec_mkbp_event evt)
{
	uint8_t t = fifo[fifo_start].event_type;
	uint8_t size;

	if (!fifo_entries)
		return -1;

	/*
	 * We need to peek at the next event to check that we were called with
	 * the correct event.
	 */
	if (t != (uint8_t)evt) {
		/*
		 * We were called with the wrong event.  The next element in the
		 * FIFO's event type doesn't match with what we were called
		 * with.  Return an error that we're busy.  The caller will need
		 * to call us with the correct event first.
		 */
		return -EC_ERROR_BUSY;
	}

	fifo_remove(out);

	/* Keep sending events if FIFO is not empty */
	if (fifo_entries)
		mkbp_send_event(fifo[fifo_start].event_type);

	/* Return the correct size of the data. */
	size = get_data_size(t);
	if (size)
		return size;
	else
		return -EC_ERROR_UNKNOWN;
}
