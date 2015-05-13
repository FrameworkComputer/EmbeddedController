/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* UART driver for emulator */

#include <pthread.h>
#include <stdio.h>
#include <termio.h>
#include <unistd.h>

#include "common.h"
#include "queue.h"
#include "task.h"
#include "test_util.h"
#include "uart.h"
#include "util.h"

static int stopped = 1;
static int int_disabled;
static int init_done;

static pthread_t input_thread;

#define INPUT_BUFFER_SIZE 16
static int char_available;

static struct queue const cached_char = QUEUE_NULL(INPUT_BUFFER_SIZE, char);

#define CONSOLE_CAPTURE_SIZE 2048
static char capture_buf[CONSOLE_CAPTURE_SIZE];
static int capture_size;
static int capture_enabled;

void test_capture_console(int enabled)
{
	if (enabled == capture_enabled)
		return;

	if (enabled)
		capture_size = 0;
	else
		capture_buf[capture_size] = '\0';

	capture_enabled = enabled;
}

static void test_capture_char(char c)
{
	if (capture_size == CONSOLE_CAPTURE_SIZE)
		return;
	capture_buf[capture_size++] = c;
}


const char *test_get_captured_console(void)
{
	return (const char *)capture_buf;
}

static void uart_interrupt(void)
{
	uart_process_input();
	uart_process_output();
}

static void trigger_interrupt(void)
{
	if (int_disabled)
		return;
	if (task_start_called())
		task_trigger_test_interrupt(uart_interrupt);
	else
		uart_interrupt();
}

int uart_init_done(void)
{
	return init_done;
}

void uart_tx_start(void)
{
	stopped = 0;
	trigger_interrupt();
}

void uart_tx_stop(void)
{
	stopped = 1;
}

int uart_tx_stopped(void)
{
	return stopped;
}

void uart_tx_flush(void)
{
	/* Nothing */
}

int uart_tx_ready(void)
{
	return 1;
}

int uart_rx_available(void)
{
	return char_available;
}

void uart_write_char(char c)
{
	if (capture_enabled)
		test_capture_char(c);
	printf("%c", c);
	fflush(stdout);
}

int uart_read_char(void)
{
	char ret;
	ASSERT(in_interrupt_context());
	queue_remove_unit(&cached_char, &ret);
	--char_available;
	return ret;
}

void uart_disable_interrupt(void)
{
	int_disabled = 1;
}

void uart_enable_interrupt(void)
{
	int_disabled = 0;
}

void uart_inject_char(char *s, int sz)
{
	int i;
	int num_char;

	for (i = 0; i < sz; i += INPUT_BUFFER_SIZE - 1) {
		num_char = MIN(INPUT_BUFFER_SIZE - 1, sz - i);
		if (queue_space(&cached_char) < num_char)
			return;
		queue_add_units(&cached_char, s + i, num_char);
		char_available = num_char;
		trigger_interrupt();
	}
}

void *uart_monitor_stdin(void *d)
{
	struct termios org_settings, new_settings;
	char buf[INPUT_BUFFER_SIZE];
	int rv;

	tcgetattr(0, &org_settings);
	new_settings = org_settings;
	new_settings.c_lflag &= ~(ECHO | ICANON);
	new_settings.c_cc[VTIME] = 0;
	new_settings.c_cc[VMIN] = 1;

	printf("Console input initialized\n");
	while (1) {
		tcsetattr(0, TCSANOW, &new_settings);
		rv = read(0, buf, INPUT_BUFFER_SIZE);
		if (queue_space(&cached_char) >= rv) {
			queue_add_units(&cached_char, buf, rv);
			char_available = rv;
		}
		tcsetattr(0, TCSANOW, &org_settings);
		/*
		 * Trigger emulated interrupt to process input. Keyboard
		 * input while interrupt handler runs is queued by the
		 * system.
		 */
		trigger_interrupt();
	}

	return 0;
}

void uart_init(void)
{
	pthread_create(&input_thread, NULL, uart_monitor_stdin, NULL);
	stopped = 1;  /* Not transmitting yet */
	init_done = 1;
}
