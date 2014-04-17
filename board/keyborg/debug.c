/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* GPIO UART debug printf */

#include "board.h"
#include "common.h"
#include "printf.h"
#include "registers.h"
#include "timer.h"
#include "util.h"

#define BAUD 9600
#define BIT_PERIOD (1000000 / BAUD)

int debug_txchar(void *context, int c)
{
	int i;
	timestamp_t st;
	int32_t d;

	if (c == '\n')
		debug_txchar(context, '\r');

	c = (c << 1) | (1 << 9);
	st = get_time();
	for (i = 0; i < 10; ++i) {
		STM32_GPIO_BSRR(GPIO_A) = 1 << ((c & 1) ? 15 : 31);
		d = MAX(st.val + BIT_PERIOD * (i + 1) - get_time().val, 0);
		if (d)
			udelay(d);
		c >>= 1;
	}

	return 0;
}

void debug_printf(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vfnprintf(debug_txchar, NULL, format, args);
	va_end(args);
}

void panic(const char *msg)
{
}
