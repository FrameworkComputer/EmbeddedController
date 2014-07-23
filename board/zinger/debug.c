/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Synchronous UART debug printf */

#include "common.h"
#include "printf.h"
#include "registers.h"
#include "util.h"

static int debug_txchar(void *context, int c)
{
	if (c == '\n') {
		while (!(STM32_USART_SR(UARTN_BASE) & STM32_USART_SR_TXE))
			;
		STM32_USART_TDR(UARTN_BASE) = '\r';
	}

	/* Wait for space to transmit */
	while (!(STM32_USART_SR(UARTN_BASE) & STM32_USART_SR_TXE))
		;
	STM32_USART_TDR(UARTN_BASE) = c;

	return 0;
}



void debug_printf(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vfnprintf(debug_txchar, NULL, format, args);
	va_end(args);
}
