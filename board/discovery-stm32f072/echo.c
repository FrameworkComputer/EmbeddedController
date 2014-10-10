/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/*
 * Task to echo any characters from the three non-console USARTs back to all
 * non-console USARTs.
 */

#include "atomic.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "panic.h"
#include "task.h"
#include "timer.h"
#include "usart-stm32f0.h"
#include "usb-stream.h"
#include "util.h"

static void in_ready(struct in_stream const *stream)
{
	task_wake(TASK_ID_ECHO);
}

static void out_ready(struct out_stream const *stream)
{
	task_wake(TASK_ID_ECHO);
}

USART_CONFIG(usart1, usart1_hw, 115200, 64, 64, in_ready, NULL)
USART_CONFIG(usart3, usart3_hw, 115200, 64, 64, in_ready, NULL)
USART_CONFIG(usart4, usart4_hw, 115200, 64, 64, in_ready, NULL)
USB_STREAM_CONFIG(usb_stream1,
		  USB_IFACE_STREAM,
		  USB_EP_STREAM,
		  256,
		  256,
		  in_ready,
		  out_ready)

const void *const usb_strings[] = {
	[USB_STR_DESC]    = usb_string_desc,
	[USB_STR_VENDOR]  = USB_STRING_DESC("Google Inc."),
	[USB_STR_PRODUCT] = USB_STRING_DESC("discovery-stm32f072"),
	[USB_STR_VERSION] = NULL /* filled at runtime */,
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

struct stream_console_state {
	size_t wrote;
};

struct stream_console_config {
	struct stream_console_state *state;

	struct in_stream  const *in;
	struct out_stream const *out;
};

#define STREAM_CONSOLE_CONFIG(NAME, IN, OUT)			\
	static struct stream_console_state NAME##_state;	\
	struct stream_console_config const NAME = {		\
		.state = &NAME##_state,				\
		.in    = IN,					\
		.out   = OUT,					\
	};

STREAM_CONSOLE_CONFIG(usart1_stream_console, &usart1.in, &usart1.out)
STREAM_CONSOLE_CONFIG(usart3_stream_console, &usart3.in, &usart3.out)
STREAM_CONSOLE_CONFIG(usart4_stream_console, &usart4.in, &usart4.out)
STREAM_CONSOLE_CONFIG(usb_stream1_console, &usb_stream1.in, &usb_stream1.out)

static struct stream_console_config const *const consoles[] = {
	&usart1_stream_console,
	&usart3_stream_console,
	&usart4_stream_console,
	&usb_stream1_console,
};

static size_t echo(struct stream_console_config const *const consoles[],
		   size_t consoles_count)
{
	size_t total = 0;
	size_t i;

	for (i = 0; i < consoles_count; ++i) {
		size_t  j;
		uint8_t buffer[64];
		size_t  remaining = 0;
		size_t  count     = in_stream_read(consoles[i]->in,
						   buffer,
						   sizeof(buffer));

		if (count == 0)
			continue;

		for (j = 0; j < consoles_count; ++j)
			consoles[j]->state->wrote = 0;

		do {
			remaining = 0;

			for (j = 0; j < consoles_count; ++j) {
				size_t wrote = consoles[j]->state->wrote;

				if (count == wrote)
					continue;

				wrote += out_stream_write(consoles[j]->out,
							  buffer + wrote,
							  count - wrote);

				consoles[j]->state->wrote = wrote;

				remaining += count - wrote;
			}
		} while (remaining);

		total += count;
	}

	return total;
}

void echo_task(void)
{
	usart_init(&usart1);
	usart_init(&usart3);
	usart_init(&usart4);

	while (1) {
		while (echo(consoles, ARRAY_SIZE(consoles))) {
			/*
			 * Make sure other tasks, like the HOOKS get to run.
			 */
			msleep(1);
		}

		/*
		 * There was nothing left to echo, go to sleep and be
		 * woken up by the next input.
		 */
		task_wait_event(-1);
	}
}

static int command_echo_info(int argc, char **argv)
{
	char const message[] = "Hello World!\r\n";
	size_t     i;

	ccprintf("USART1 RX dropped %d bytes\n",
		 atomic_read_clear((uint32_t *) &(usart1.state->rx_dropped)));

	ccprintf("USART3 RX dropped %d bytes\n",
		 atomic_read_clear((uint32_t *) &(usart3.state->rx_dropped)));

	ccprintf("USART4 RX dropped %d bytes\n",
		 atomic_read_clear((uint32_t *) &(usart4.state->rx_dropped)));

	for (i = 0; i < ARRAY_SIZE(consoles); ++i)
		out_stream_write(consoles[i]->out, message, strlen(message));

	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(echo_info,
			command_echo_info,
			NULL,
			"Dump echo task debug info",
			NULL);
