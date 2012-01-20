/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Chrome OS EC i8042 interface code.
 */

#include "board.h"
#include "common.h"
#include "i8042.h"
#include "keyboard.h"
#include "lpc.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"


#define I8042_DEBUG 1

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
void i8042_init()
{
	head_to_buffer = tail_to_buffer = 0;
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

	/* TODO: need atomic protection */
	if ((tail_to_buffer + len) <= (head_to_buffer + HOST_BUFFER_SIZE - 1)) {
		for (from = 0, to = tail_to_buffer; from < len;) {
			to_host_buffer[to++] = to_host[from++];
			to %= HOST_BUFFER_SIZE;
		}
		tail_to_buffer = (tail_to_buffer + len) % HOST_BUFFER_SIZE;
	}
	/* end of atomic protection */
}


/* Called by common/keyboard.c when the host wants to receive keyboard IRQ
 * (or not).
 */
void i8042_enable_keyboard_irq(void) {
	i8042_irq_enabled = 1;
}

void i8042_disable_keyboard_irq(void) {
	i8042_irq_enabled = 0;
}


void i8042_command_task(void)
{
	while (1) {
		/* Either a new byte to host or host picking up can un-block. */
		task_wait_msg(-1);

		while (1) {
			uint8_t chr;
			int empty = 0;

			/* TODO: need atomic protection */
			if (head_to_buffer == tail_to_buffer) {
				empty = 1;  /* nothing to host */
			}
			/* end of atomic protection */
			if (empty) break;

			/* if the host still didn't read that away,
			   try next time. */
			if (lpc_keyboard_has_char()) {
#if I8042_DEBUG >= 5
				uart_printf("[%d] i8042_command_task() "
					    "cannot send to host due to host "
					    "havn't taken away.\n",
					    get_time().le.lo);
#endif
				break;
			}

			/* TODO: need atomic protection */
			chr = to_host_buffer[head_to_buffer];
			head_to_buffer =
				(head_to_buffer + 1) % HOST_BUFFER_SIZE;
			/* end of atomic protection */

			/* Write to host. */
			lpc_keyboard_put_char(chr, i8042_irq_enabled);
#if I8042_DEBUG >= 4
			uart_printf("[%d] i8042_command_task() "
				    "sends to host: 0x%02x\n",
				    get_time().le.lo, chr);
#endif
		}
	}
}


enum ec_error_list i8042_send_to_host(int len, uint8_t *to_host)
{
	enq_to_host(len, to_host);

	/* Wake up the task to handle the command */
	task_send_msg(TASK_ID_I8042CMD, TASK_ID_I8042CMD, 0);

	return EC_SUCCESS;
}
