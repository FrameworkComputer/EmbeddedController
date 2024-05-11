/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Printf-like functionality for Chrome EC */

#ifndef __CROS_EC_PRINTF_H
#define __CROS_EC_PRINTF_H

#include "common.h"
#include "console.h"

#include <stdarg.h> /* For va_list */
#include <stdbool.h>
#include <stddef.h> /* For size_t */
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Buffer size in bytes large enough to hold the largest possible timestamp.
 */
#define PRINTF_TIMESTAMP_BUF_SIZE 22

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
 *   - 'i' - signed integer (if CONFIG_PRINTF_LONG_IS_32BITS is enabled)
 *   - 'u' - unsigned integer
 *   - 'x' - unsigned integer, print as lower-case hexadecimal
 *   - 'X' - unsigned integer, print as upper-case hexadecimal
 *   - 'b' - unsigned integer, print as binary
 *   - 'p' - pointer
 */

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

#ifdef TEST_BUILD
/**
 * Converts @val to a string written in @buf. The value is converted from
 * least-significant digit to most-significant digit, so the pointer returned
 * does not necessarily point to the start of @buf.
 *
 * This function shouldn't be used directly; it's a helper function for other
 * printf functions and only exposed for testing.
 *
 * @param[out] buf Destination buffer
 * @param[in] buf_len Length of @buf in bytes
 * @param[in] val Value to convert
 * @param[in] precision Fixed point precision; -1 disables fixed point
 * @param[in] base Base
 * @param[in] uppercase true to print hex characters uppercase
 * @return pointer to start of string on success (not necessarily the start of
 * @buf).
 * @return NULL on error
 */
char *uint64_to_str(char *buf, int buf_len, uint64_t val, int precision,
		    int base, bool uppercase);
#endif /* TEST_BUILD */

/**
 * Print timestamp as string to the provided buffer.
 *
 * Guarantees NUL-termination if size != 0.
 *
 * @param[out] str Destination string
 * @param[in] size Size of @str in bytes
 * @param[in] timestamp Timestamp
 * @return Length of string written to @str, not including terminating NUL.
 * @return -EC_ERROR_OVERFLOW when @str buffer is not large enough. @str[0]
 * is set to '\0'.
 * @return -EC_ERROR_INVAL when @size is 0.
 */
int snprintf_timestamp(char *str, size_t size, uint64_t timestamp);

/**
 * Print the current time as a string to the provided buffer.
 *
 * Guarantees NUL-termination if size != 0.
 *
 * @param[out] str Destination string
 * @param[in] size Size of @str in bytes
 * @return Length of string written to @str, not including terminating NUL.
 * @return -EC_ERROR_OVERFLOW when @str buffer is not large enough. @str[0]
 * is set to '\0'.
 * @return -EC_ERROR_INVAL when @size is 0.
 */
int snprintf_timestamp_now(char *str, size_t size);

/**
 * Prints bytes as a hex string in the provided buffer.
 *
 * Guarantees NUL-termination if size != 0.
 *
 * @param[out] str Destination string
 * @param[in] size Size of @str in bytes
 * @param[in] params Data to print
 * @return Length of string written to @str, not including terminating NUL.
 * @return -EC_ERROR_OVERFLOW when @str buffer is not large enough.
 * @return -EC_ERROR_INVAL when @size is 0.
 */
int snprintf_hex_buffer(char *str, size_t size,
			const struct hex_buffer_params *params);

/**
 * @param[in] num_bytes
 * @return number of bytes needed to store @num_bytes as a string (including
 * terminating '\0').
 */
size_t hex_str_buf_size(size_t num_bytes);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_PRINTF_H */
