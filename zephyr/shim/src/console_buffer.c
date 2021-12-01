/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <kernel.h>
#include <zephyr.h>

#include "common.h"
#include "console.h"
#include "ec_commands.h"

static char console_buf[CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_BUF_SIZE];
static uint32_t previous_snapshot_idx;
static uint32_t current_snapshot_idx;
static uint32_t head_idx;
static uint32_t tail_idx;

static inline uint32_t next_idx(uint32_t cur_idx)
{
	return (cur_idx + 1) % ARRAY_SIZE(console_buf);
}

K_MUTEX_DEFINE(console_write_lock);

void console_buf_notify_chars(const char *s, size_t len)
{
	/*
	 * This is just notifying of console characters for debugging
	 * output, so if we are unable to lock the mutex immediately,
	 * then just drop the string.
	 */
	if (k_mutex_lock(&console_write_lock, K_NO_WAIT))
		return;
	/* We got the mutex. */
	while (len--) {
		/* Don't copy null byte into buffer */
		if (!(*s))
			continue;

		uint32_t new_tail = next_idx(tail_idx);

		/* Check if we are starting to overwrite our snapshot
		 * heads
		 */
		if (new_tail == head_idx)
			head_idx = next_idx(head_idx);
		if (new_tail == previous_snapshot_idx)
			previous_snapshot_idx =
				next_idx(previous_snapshot_idx);
		if (new_tail == current_snapshot_idx)
			current_snapshot_idx =
				next_idx(current_snapshot_idx);

		console_buf[tail_idx] = *s++;
		tail_idx = new_tail;
	}
	k_mutex_unlock(&console_write_lock);
}

enum ec_status uart_console_read_buffer_init(void)
{
	if (k_mutex_lock(&console_write_lock, K_MSEC(100)))
		/* Failed to acquire console buffer mutex */
		return EC_RES_TIMEOUT;

	previous_snapshot_idx = current_snapshot_idx;
	current_snapshot_idx = head_idx;

	k_mutex_unlock(&console_write_lock);

	return EC_RES_SUCCESS;
}

int uart_console_read_buffer(uint8_t type, char *dest, uint16_t dest_size,
			     uint16_t *write_count_out)
{
	uint32_t *head;
	uint16_t write_count = 0;

	switch (type) {
	case CONSOLE_READ_NEXT:
		/* Start from beginning of latest snapshot */
		head = &current_snapshot_idx;
		break;
	case CONSOLE_READ_RECENT:
		/* Start from end of previous snapshot */
		head = &previous_snapshot_idx;
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	/* We need to make sure we have room for at least the null byte */
	if (dest_size == 0)
		return EC_RES_INVALID_PARAM;

	if (k_mutex_lock(&console_write_lock, K_MSEC(100)))
		/* Failed to acquire console buffer mutex */
		return EC_RES_TIMEOUT;

	if (*head == tail_idx) {
		/* No new data, return empty response */
		k_mutex_unlock(&console_write_lock);
		return EC_RES_SUCCESS;
	}

	do {
		if (write_count >= dest_size - 1)
			/* Buffer is full, minus the space for a null byte */
			break;

		dest[write_count] = console_buf[*head];
		write_count++;
		*head = next_idx(*head);
	} while (*head != tail_idx);

	dest[write_count] = '\0';
	write_count++;

	*write_count_out = write_count;
	k_mutex_unlock(&console_write_lock);

	return EC_RES_SUCCESS;
}

/* ECOS uart buffer, putc is blocking instead. */
int uart_buffer_full(void)
{
	return false;
}
