/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* SCP UART module */

#include "uart.h"

void uart_init(void)
{
}

int uart_init_done(void)
{
	return 0;
}

void uart_tx_flush(void)
{
}

int uart_tx_ready(void)
{
	return 0;
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

void uart_tx_start(void)
{
}

void uart_tx_stop(void)
{
}

void uart_process(void)
{
}
