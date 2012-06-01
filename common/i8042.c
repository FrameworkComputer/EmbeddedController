/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Chrome OS EC i8042 interface code.
 */

#include "board.h"
#include "common.h"
#include "console.h"
#include "i8042.h"
#include "keyboard.h"
#include "lpc.h"
#include "queue.h"
#include "task.h"
#include "timer.h"
#include "util.h"


#define I8042_DEBUG 1

/* Console output macros */
#if I8042_DEBUG >= 4
#define CPRINTF4(format, args...) cprintf(CC_I8042, format, ## args)
#else
#define CPRINTF4(format, args...)
#endif
#if I8042_DEBUG >= 5
#define CPRINTF5(format, args...) cprintf(CC_I8042, format, ## args)
#else
#define CPRINTF5(format, args...)
#endif


static int i8042_irq_enabled;


static struct mutex to_host_mutex;
static uint8_t to_host_buffer[16];
static struct queue to_host = {
	.buf_bytes  = ARRAY_SIZE(to_host_buffer),
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
	.buf_bytes  = ARRAY_SIZE(from_host_buffer),
	.unit_bytes = sizeof(struct host_byte),
	.buf        = from_host_buffer,
};


/* Reset all i8042 buffer */
void i8042_flush_buffer()
{
	queue_reset(&to_host);
	lpc_keyboard_clear_buffer();
}


/* Called by the chip-specific code when host sends a byte to port 0x60.
 * Note that this is in the interrupt context.
 */
void i8042_receives_data(int data)
{
	struct host_byte h;

	h.type = HOST_DATA;
	h.byte = data;
	queue_add_units(&from_host, &h, 1);
	task_wake(TASK_ID_I8042CMD);
}


/* Called by the chip-specific code when host sends a byte to port 0x64.
 * Note that this is in the interrupt context.
 */
void i8042_receives_command(int cmd)
{
	struct host_byte h;

	h.type = HOST_COMMAND;
	h.byte = cmd;
	queue_add_units(&from_host, &h, 1);
	task_wake(TASK_ID_I8042CMD);
}


/* Called by common/keyboard.c when the host wants to receive keyboard IRQ
 * (or not).
 */
void i8042_enable_keyboard_irq(void) {
	i8042_irq_enabled = 1;
	lpc_keyboard_resume_irq();
}

void i8042_disable_keyboard_irq(void) {
	i8042_irq_enabled = 0;
}


static void i8042_handle_from_host(void)
{
	struct host_byte h;
	int ret_len;
	uint8_t output[MAX_SCAN_CODE_LEN];
	enum ec_error_list ret;

	while (queue_remove_unit(&from_host, &h)) {
		if (h.type == HOST_COMMAND)
			ret_len = handle_keyboard_command(h.byte, output);
		else
			ret_len = handle_keyboard_data(h.byte, output);

		ret = i8042_send_to_host(ret_len, output);
		ASSERT(ret == EC_SUCCESS);
	}
}

void i8042_command_task(void)
{
	while (1) {
		/* Either a new byte to host or host picking up can un-block. */
		task_wait_event(-1);

		while (1) {
			uint8_t chr;

			/* first handle command/data from host. */
			i8042_handle_from_host();

			/* Check if we have data in buffer to host. */
			if (queue_is_empty(&to_host))
				break;  /* nothing to host */

			/* if the host still didn't read that away,
			   try next time. */
			if (lpc_keyboard_has_char()) {
				CPRINTF5("[%T i8042_command_task() "
					 "cannot send to host due to host "
					 "haven't taken away.\n");
				break;
			}

			/* Get a char from buffer. */
			kblog_put('k', to_host.head);
			queue_remove_unit(&to_host, &chr);
			kblog_put('K', chr);

			/* Write to host. */
			lpc_keyboard_put_char(chr, i8042_irq_enabled);
			CPRINTF4("[%T i8042_command_task() "
				 "sends to host: 0x%02x\n", chr);
		}
	}
}


static void enq_to_host(int len, const uint8_t *bytes)
{
	int i;

	mutex_lock(&to_host_mutex);
	/* Check if the buffer has enough space, then copy them to buffer. */
	if (queue_has_space(&to_host, len)) {
		for (i = 0; i < len; ++i) {
			kblog_put('t', to_host.tail);
			kblog_put('T', bytes[i]);
		}
		queue_add_units(&to_host, bytes, len);
	}
	mutex_unlock(&to_host_mutex);
}

enum ec_error_list i8042_send_to_host(int len, const uint8_t *bytes)
{
	int i;

	for (i = 0; i < len; i++)
		kblog_put('s', bytes[i]);

	/* Put to queue in memory */
	enq_to_host(len, bytes);

	/* Wake up the task to move from queue to the buffer to host. */
	task_wake(TASK_ID_I8042CMD);

	return EC_SUCCESS;
}
