/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* UART driver for emulator */

#include <pthread.h>
#include <signal.h>
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
static int init_done;

#ifndef TEST_FUZZ
static pthread_t input_thread;
#endif

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

int uart_init_done(void)
{
	return init_done;
}

void uart_tx_start(void)
{
	stopped = 0;
	task_trigger_test_interrupt(uart_interrupt);
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
		task_trigger_test_interrupt(uart_interrupt);
	}
}

/*
 * We do not really need console input when fuzzing, and having it enabled
 * breaks terminal when an error is detected.
 */
#ifndef TEST_FUZZ
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t uart_monitor_initialized = PTHREAD_COND_INITIALIZER;

void *uart_monitor_stdin(void *d)
{
	struct termios org_settings, new_settings;
	char buf[INPUT_BUFFER_SIZE];
	int rv;

	pthread_mutex_lock(&mutex);
	tcgetattr(0, &org_settings);
	new_settings = org_settings;
	new_settings.c_lflag &= ~(ECHO | ICANON);
	new_settings.c_cc[VTIME] = 0;
	new_settings.c_cc[VMIN] = 1;

	printf("Console input initialized\n");
	/* Allow uart_init to proceed now that UART monitor is initialized. */
	pthread_cond_signal(&uart_monitor_initialized);
	pthread_mutex_unlock(&mutex);
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
		task_trigger_test_interrupt(uart_interrupt);
	}

	return 0;
}
#endif /* !TEST_FUZZ */

void uart_init(void)
{
#ifndef TEST_FUZZ
	/* Create UART monitor thread and wait for it to initialize. */
	pthread_mutex_lock(&mutex);
	pthread_create(&input_thread, NULL, uart_monitor_stdin, NULL);
	pthread_cond_wait(&uart_monitor_initialized, &mutex);
	pthread_mutex_unlock(&mutex);
#endif

	stopped = 1;  /* Not transmitting yet */
	init_done = 1;
}
