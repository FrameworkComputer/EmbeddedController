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
/* TODO: Code in common.c should not directly access chip registers */
#include "registers.h"
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


void i8042_init()
{
	head_to_buffer = tail_to_buffer = 0;
	LM4_LPC_ST(LPC_CH_KEYBOARD) = 0;  /* clear the TOH bit */
}


void i8042_receives_data(int data)
{
	int ret_len;
	uint8_t output[MAX_SCAN_CODE_LEN];
	enum ec_error_list ret;

	ret_len = handle_keyboard_data(data, output);
	ret = i8042_send_to_host(ret_len, output);
	ASSERT(ret == EC_SUCCESS);
}


void i8042_receives_command(int cmd)
{
	int ret_len;
	uint8_t output[MAX_SCAN_CODE_LEN];
	enum ec_error_list ret;

	ret_len = handle_keyboard_command(cmd, output);
	ret = i8042_send_to_host(ret_len, output);
	ASSERT(ret == EC_SUCCESS);
}


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
			if (LM4_LPC_ST(LPC_CH_KEYBOARD) & (1 << 0 /* TOH */)) {
#if I8042_DEBUG >= 5
				uart_printf("[%d] i8042_command_task() "
					    "cannot send to host due to TOH\n",
					    get_time().le.lo);
#endif
				break;
			}

			/* TODO: need atomic protection */
			chr = to_host_buffer[head_to_buffer];
			head_to_buffer =
				(head_to_buffer + 1) % HOST_BUFFER_SIZE;
			/* end of atomic protection */

			/* Write to host. TOH is set automatically. */
			LPC_POOL_KEYBOARD[1] = chr;
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
