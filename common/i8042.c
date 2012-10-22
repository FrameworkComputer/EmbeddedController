/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * i8042 interface to host
 *
 * i8042 commands are processed by keyboard.c.
 */

#include "common.h"
#include "config.h"
#include "console.h"
#include "i8042.h"
#include "keyboard.h"
#include "lpc.h"
#include "queue.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_I8042, format, ## args)

static int i8042_irq_enabled;

/*
 * Mutex to control write access to the to-host buffer head.  Don't need to
 * mutex the tail because reads are only done in one place.
 */
static struct mutex to_host_mutex;

static uint8_t to_host_buffer[16];
static struct queue to_host = {
	.buf_bytes  = sizeof(to_host_buffer),
	.unit_bytes = sizeof(uint8_t),
	.buf        = to_host_buffer,
};

/* Queue command/data from the host */
enum {
	HOST_COMMAND = 0,
	HOST_DATA,
};
struct host_byte {
	uint8_t type;
	uint8_t byte;
};

/* 4 is big enough for all i8042 commands */
static uint8_t from_host_buffer[4 * sizeof(struct host_byte)];
static struct queue from_host = {
	.buf_bytes  = sizeof(from_host_buffer),
	.unit_bytes = sizeof(struct host_byte),
	.buf        = from_host_buffer,
};

void i8042_flush_buffer()
{
	mutex_lock(&to_host_mutex);
	queue_reset(&to_host);
	mutex_unlock(&to_host_mutex);
	lpc_keyboard_clear_buffer();
}

void i8042_receive(int data, int is_cmd)
{
	struct host_byte h;

	h.type = is_cmd ? HOST_COMMAND : HOST_DATA;
	h.byte = data;
	queue_add_units(&from_host, &h, 1);
	task_wake(TASK_ID_I8042CMD);
}

void i8042_enable_keyboard_irq(int enable)
{
	i8042_irq_enabled = enable;
	if (enable)
		lpc_keyboard_resume_irq();
}

static void i8042_handle_from_host(void)
{
	struct host_byte h;
	int ret_len;
	uint8_t output[MAX_SCAN_CODE_LEN];

	while (queue_remove_unit(&from_host, &h)) {
		if (h.type == HOST_COMMAND)
			ret_len = handle_keyboard_command(h.byte, output);
		else
			ret_len = handle_keyboard_data(h.byte, output);

		i8042_send_to_host(ret_len, output);
	}
}

void i8042_command_task(void)
{
	while (1) {
		/* Wait for next host read/write */
		task_wait_event(-1);

		while (1) {
			uint8_t chr;

			/* Handle command/data write from host */
			i8042_handle_from_host();

			/* Check if we have data to send to host */
			if (queue_is_empty(&to_host))
				break;

			/* Host interface must have space */
			if (lpc_keyboard_has_char())
				break;

			/* Get a char from buffer. */
			kblog_put('k', to_host.head);
			queue_remove_unit(&to_host, &chr);
			kblog_put('K', chr);

			/* Write to host. */
			lpc_keyboard_put_char(chr, i8042_irq_enabled);
		}
	}
}

void i8042_send_to_host(int len, const uint8_t *bytes)
{
	int i;

	for (i = 0; i < len; i++)
		kblog_put('s', bytes[i]);

	/* Enqueue output data if there's space */
	mutex_lock(&to_host_mutex);
	if (queue_has_space(&to_host, len)) {
		kblog_put('t', to_host.tail);
		queue_add_units(&to_host, bytes, len);
	}
	mutex_unlock(&to_host_mutex);

	/* Wake up the task to move from queue to host */
	task_wake(TASK_ID_I8042CMD);
}
