/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "debug_printf.h"

#include "printf.h"
#include "uart.h"

#include "stddef.h"

static int printchar(void *context, int c)
{
	if (c == '\n')
		uart_write_char('\r');
	uart_write_char(c);

	return 0;
}

void debug_printf(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vfnprintf(printchar, NULL, format, args);
	va_end(args);
}
