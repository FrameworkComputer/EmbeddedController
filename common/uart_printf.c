/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "panic_log.h"
#include "printf.h"
#include "uart.h"

#include <stddef.h>

static int __tx_char(void *context, int c)
{
	if (IS_ENABLED(CONFIG_PANIC_LOG))
		panic_log_write_char(c);
	/*
	 * Translate '\n' to '\r\n'.
	 */
	if (c == '\n' && uart_tx_char_raw(context, '\r'))
		return 1;
	return uart_tx_char_raw(context, c);
}

int uart_putc(int c)
{
	int rv = __tx_char(NULL, c);

	uart_tx_start();

	return rv ? EC_ERROR_OVERFLOW : EC_SUCCESS;
}

int uart_puts(const char *outstr)
{
	/* Put all characters in the output buffer */
	for (; *outstr != '\0'; ++outstr) {
		if (__tx_char(NULL, *outstr) != 0) {
			break;
		}
	}

	uart_tx_start();

	/* Successful if we consumed all output */
	return *outstr ? EC_ERROR_OVERFLOW : EC_SUCCESS;
}

int uart_put(const char *out, int len)
{
	int written;

	/* Put all characters in the output buffer */
	for (written = 0; written < len; written++) {
		if (__tx_char(NULL, *out++) != 0) {
			break;
		}
	}

	uart_tx_start();

	return written;
}

int uart_put_raw(const char *out, int len)
{
	int written;

	/* Put all characters in the output buffer */
	for (written = 0; written < len; written++) {
		if (uart_tx_char_raw(NULL, *out++) != 0) {
			break;
		}
	}

	uart_tx_start();

	return written;
}

int uart_vprintf(const char *format, va_list args)
{
	int rv = vfnprintf(__tx_char, NULL, format, args);

	uart_tx_start();

	return rv;
}

int uart_printf(const char *format, ...)
{
	int rv;
	va_list args;

	va_start(args, format);
	rv = uart_vprintf(format, args);
	va_end(args);
	return rv;
}
