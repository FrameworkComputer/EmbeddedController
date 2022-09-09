/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* FPC Platform Abstraction Layer */

#include <stdint.h>
#include <stddef.h>

#include "shared_mem.h"
#include "uart.h"

void __unused *fpc_malloc(uint32_t size)
{
	char *data;
	int rc;

	rc = shared_mem_acquire(size, (char **)&data);

	if (rc == 0)
		return data;
	else
		return NULL;
}

void __unused fpc_free(void *data)
{
	shared_mem_release(data);
}

/* Not in release */
void __unused fpc_assert_fail(const char *file, uint32_t line, const char *func,
			      const char *expr)
{
}

void __unused fpc_log_var(const char *source, uint8_t level, const char *format,
			  ...)
{
	va_list args;

	va_start(args, format);
	uart_vprintf(format, args);
	va_end(args);
}

uint32_t abs(int32_t a)
{
	return (a < 0) ? (uint32_t)(-a) : (uint32_t)a;
}
