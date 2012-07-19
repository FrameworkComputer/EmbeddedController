/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Printf-like functionality for Chrome EC */

#ifndef __CROS_EC_PRINTF_H
#define __CROS_EC_PRINTF_H

#include <stdarg.h>  /* For va_list */
#include "common.h"

/*
 * Printf formatting: % [flags] [width] [.precision] [length] [type]
 *
 * Flags may be any/all of the following, and must occur in the following
 * order if present:
 *   - '0' = prefixed with 0's instead of spaces (%08x)
 *   - '-' = left-justify instead of right-justify (%-5s)
 *
 * Width is the minimum output width, and may be:
 *   - A number ("0" - "255")
 *   - '*' = use next integer argument as width
 *
 * Precision must be preceded by a decimal point, and may be:
 *   - A number ("0" - "255")
 *   - '*' = use next integer argument as precision
 *
 * For integers, precision will put a decimal point before that many digits.
 * So snprintf(buf, size, "%.6d", 123) sets buf="0.000123".  This is most
 * useful for printing times, voltages, and currents.
 *
 * Length may be:
 *   - 'l' = integer is 64-bit instead of native 32-bit
 *
 * Type may be:
 *   - 'c' - character
 *   - 's' - null-terminated ASCII string
 *   - 'h' - binary data, print as hex; precision is length of data in bytes.
 *           So "%.8h" prints 8 bytes of binary data
 *   - 'p' - pointer
 *   - 'd' - signed integer
 *   - 'u' - unsigned integer
 *   - 'x' - unsigned integer, print as lower-case hexadecimal
 *   - 'X' - unsigned integer, print as upper-case hexadecimal
 *   - 'b' - unsigned integer, print as binary
 *
 * Special format codes:
 *   - "%T" - current time in seconds - interpreted as "%.6T" for precision.
 *           This does NOT use up any arguments.
 */

/**
 * Print formatted output to a function, like vfprintf()
 *
 * addchar() will be called for every character to be printed, with the context
 * pointer passed to vfnprintf().  addchar() should return 0 if the character
 * was accepted or non-zero if the character was dropped due to overflow.
 *
 * Returns error if output was truncated.
 */
int vfnprintf(int (*addchar)(void *context, int c), void *context,
	      const char *format, va_list args);


/* Print formatted outut to a string */
int snprintf(char *str, int size, const char *format, ...);


#endif  /* __CROS_EC_PRINTF_H */
