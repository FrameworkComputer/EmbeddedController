/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* UART driver for emulator */

#include <stdio.h>

#include "board.h"
#include "config.h"
#include "task.h"
#include "uart.h"

static int stopped;
static int int_disabled;
static int init_done;

static void trigger_interrupt(void)
{
	if (!int_disabled)
		uart_process();
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
	return 0;
}

void uart_write_char(char c)
{
	printf("%c", c);
	fflush(stdout);
}

int uart_read_char(void)
{
	/* Should never be called for now */
	return 0;
}

void uart_disable_interrupt(void)
{
	int_disabled = 1;
}

void uart_enable_interrupt(void)
{
	int_disabled = 0;
}

void uart_init(void)
{
	init_done = 1;
}
