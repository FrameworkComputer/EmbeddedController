/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USART driver for Chrome EC */

#include <stdarg.h>

#include "task.h"
#include "uart.h"

/* Baud rate for UARTs */
#define BAUD_RATE 115200

void uart_tx_start(void)
{
}

void uart_tx_stop(void)
{
}

int uart_tx_stopped(void)
{
	return 0;
}

void uart_tx_flush(void)
{
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
}

int uart_read_char(void)
{
	return 0;
}

void uart_disable_interrupt(void)
{
}

void uart_enable_interrupt(void)
{
}

int uart_init(void)
{
	return EC_SUCCESS;
}
