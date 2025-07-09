/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "plat_log.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef EGIS_DBG
LOG_LEVEL g_log_level = LOG_DEBUG;
#else
LOG_LEVEL g_log_level = LOG_INFO;
#endif

static char printf_buffer[256]; // emflibrary debug buffer

void set_debug_level(LOG_LEVEL level)
{
	g_log_level = level;
	output_log(LOG_ERROR, "RBS", "", "", 0, "set_debug_level %d", level);
}

void output_log(LOG_LEVEL level, const char *tag, const char *file_path,
		const char *func, int line, const char *format, ...)
{
	if (format == NULL)
		return;
	if (g_log_level > level)
		return;

	va_list vl;
	va_start(vl, format);
	int n = snprintf(printf_buffer, sizeof(printf_buffer), "%s<%s:%d> ",
			 level == LOG_ERROR ? "Error~! " : "", func, line);
	n += vsnprintf(printf_buffer + n, sizeof(printf_buffer) - n, format,
		       vl);
	va_end(vl);

	switch (level) {
	case LOG_ERROR:
	case LOG_INFO:
	case LOG_DEBUG:
	case LOG_VERBOSE:
		CPRINTS("%s", printf_buffer);
		break;
	default:
		break;
	}
}
