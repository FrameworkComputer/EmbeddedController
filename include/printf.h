/* Copyright 2012 The Chromium OS Authors. All rights reserved.
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
 *   - '+' = prefix positive value with '+' (%+d). Write '%-+' instead of
 *           '%+-' when used with left-justification. Ignored when
 *           used with unsigned integer types or non-integer types.
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
 *   - 'l'  = DEPRECATED, do not use (see crbug.com/984041)
 *   - 'll' = integer is 64-bit
 *   - 'z'  = integer is sizeof(size_t)
 *
 * Type may be:
 *   - 'c' - character
 *   - 's' - null-terminated ASCII string
 *   - 'd' - signed integer
 *   - 'i' - signed integer if CONFIG_PRINTF_LEGACY_LI_FORMAT is set (ignore l)
 *   - 'u' - unsigned integer
 *   - 'x' - unsigned integer, print as lower-case hexadecimal
 *   - 'X' - unsigned integer, print as upper-case hexadecimal
 *   - 'b' - unsigned integer, print as binary
 *
 * Special format codes:
 *   - '%ph' - binary data, print as hex; Use HEX_BUF(buffer, size) to encode
 *             parameters.
 *   - '%pP' - raw pointer.
 *   - "%pT" - current time in seconds - interpreted as "%.6T" for precision.
 *           Supply PRINTF_TIMESTAMP_NOW to use the current time, or supply a
 *           pointer to a 64-bit timestamp to print.
 */

#ifndef HIDE_EC_STDLIB

/**
 * Print formatted output to a function, like vfprintf()
 *
 * @param addchar	Function to be called for each character added.
 *			Will be passed the same context passed to vfnprintf(),
 *			and the character to add.  Should return 0 if the
 *			character was accepted or non-zero if the character
 *			was dropped due to overflow.
 * @param context	Context pointer to pass to addchar()
 * @param format	Format string (see above for acceptable formats)
 * @param args		Parameters
 * @return EC_SUCCESS, or EC_ERROR_OVERFLOW if the output was truncated.
 */
__stdlib_compat int vfnprintf(int (*addchar)(void *context, int c),
			      void *context, const char *format, va_list args);

/**
 * Print formatted outut to a string.
 *
 * Guarantees null-termination if size!=0.
 *
 * @param str		Destination string
 * @param size		Size of destination in bytes
 * @param format	Format string
 * @return EC_SUCCESS, or EC_ERROR_OVERFLOW if the output was truncated.
 */
__attribute__((__format__(__printf__, 3, 4)))
__stdlib_compat int snprintf(char *str, int size, const char *format, ...);

/**
 * Print formatted output to a string.
 *
 * Guarantees null-termination if size!=0.
 *
 * @param str		Destination string
 * @param size		Size of destination in bytes
 * @param format	Format string
 * @param args		Parameters
 * @return The string length written to str, or a negative value on error.
 *         The negative values can be -EC_ERROR_INVAL or -EC_ERROR_OVERFLOW.
 */
__stdlib_compat int vsnprintf(char *str, int size, const char *format,
			      va_list args);

#endif  /* !HIDE_EC_STDLIB */

#endif  /* __CROS_EC_PRINTF_H */
