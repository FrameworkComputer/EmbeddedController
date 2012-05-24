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

#define MAX_QUEUED_KEY_PRESS 16

/* Circular buffer to host.
 * head: next to dequeqe
 * tail: next to enqueue
 * head == tail: empty.
 * tail + 1 == head: full
 */
static int head_to_buffer = 0;
static int tail_to_buffer = 0;
#define HOST_BUFFER_SIZE (16)
static uint8_t to_host_buffer[HOST_BUFFER_SIZE];

static int i8042_irq_enabled = 0;


/* Reset all i8042 buffer */
void i8042_flush_buffer()
{
	head_to_buffer = tail_to_buffer = 0;
	keyboard_clear_buffer();
}


/* Called by the chip-specific code when host sedns a byte to port 0x60. */
void i8042_receives_data(int data)
{
	int ret_len;
	uint8_t output[MAX_SCAN_CODE_LEN];
	enum ec_error_list ret;

	ret_len = handle_keyboard_data(data, output);
	ret = i8042_send_to_host(ret_len, output);
	ASSERT(ret == EC_SUCCESS);
}


/* Called by the chip-specific code when host sedns a byte to port 0x64. */
void i8042_receives_command(int cmd)
{
	int ret_len;
	uint8_t output[MAX_SCAN_CODE_LEN];
	enum ec_error_list ret;

	ret_len = handle_keyboard_command(cmd, output);
	ret = i8042_send_to_host(ret_len, output);
	ASSERT(ret == EC_SUCCESS);
}


/* Called by EC common code to send bytes to host via port 0x60. */
static void enq_to_host(int len, uint8_t *to_host)
{
	int from, to;

	/* Check if the buffer has enough space, then copy them to buffer. */
	if ((tail_to_buffer + len) <= (head_to_buffer + HOST_BUFFER_SIZE - 1)) {
		for (from = 0, to = tail_to_buffer; from < len;) {
			kblog_put('t', to);
			kblog_put('T', to_host[from]);
			to_host_buffer[to++] = to_host[from++];
			to %= HOST_BUFFER_SIZE;
		}
		tail_to_buffer = (tail_to_buffer + len) % HOST_BUFFER_SIZE;
	}
}


/* Called by common/keyboard.c when the host wants to receive keyboard IRQ
 * (or not).
 */
void i8042_enable_keyboard_irq(void) {
	i8042_irq_enabled = 1;
	keyboard_resume_interrupt();
}

void i8042_disable_keyboard_irq(void) {
	i8042_irq_enabled = 0;
}


void i8042_command_task(void)
{
	while (1) {
		/* Either a new byte to host or host picking up can un-block. */
		task_wait_event(-1);

		while (1) {
			uint8_t chr;
			int empty = 0;

			/* Check if we have data in buffer to host. */
			if (head_to_buffer == tail_to_buffer) {
				empty = 1;  /* nothing to host */
			}
			if (empty) break;

			/* if the host still didn't read that away,
			   try next time. */
			if (keyboard_has_char()) {
				CPRINTF5("[%T i8042_command_task() "
					 "cannot send to host due to host "
					 "haven't taken away.\n");
				break;
			}

			/* Get a char from buffer. */
			chr = to_host_buffer[head_to_buffer];
			kblog_put('k', head_to_buffer);
			kblog_put('K', chr);
			head_to_buffer =
				(head_to_buffer + 1) % HOST_BUFFER_SIZE;

			/* Write to host. */
			keyboard_put_char(chr, i8042_irq_enabled);
			CPRINTF4("[%T i8042_command_task() "
				 "sends to host: 0x%02x\n", chr);
		}
	}
}


enum ec_error_list i8042_send_to_host(int len, uint8_t *to_host)
{
	int i;

	for (i = 0; i < len; i++)
		kblog_put('s', to_host[i]);

	/* Put to queue in memory */
	enq_to_host(len, to_host);

	/* Wake up the task to move from queue to the buffer to host. */
	task_wake(TASK_ID_I8042CMD);

	return EC_SUCCESS;
}
