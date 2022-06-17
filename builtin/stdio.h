/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_STDIO_H__
#define __CROS_EC_STDIO_H__

#include <stddef.h>
#include <stdarg.h>

#include "common.h"

/**
 * Print formatted outut to a string.
 *
 * Guarantees null-termination if size!=0.
 *
 * @param str           Destination string
 * @param size          Size of destination in bytes
 * @param format        Format string
 * @return EC_SUCCESS, or EC_ERROR_OVERFLOW if the output was truncated.
 */
__attribute__((__format__(__printf__, 3, 4)))
__warn_unused_result __stdlib_compat int
crec_snprintf(char *str, size_t size, const char *format, ...);

/**
 * Print formatted output to a string.
 *
 * Guarantees null-termination if size!=0.
 *
 * @param str           Destination string
 * @param size          Size of destination in bytes
 * @param format        Format string
 * @param args          Parameters
 * @return The string length written to str, or a negative value on error.
 *         The negative values can be -EC_ERROR_INVAL or -EC_ERROR_OVERFLOW.
 */
__warn_unused_result __stdlib_compat int
crec_vsnprintf(char *str, size_t size, const char *format, va_list args);

/*
 * Create weak aliases to the crec_* printf functions. This lets us call the
 * crec_* printf functions in tests that link the C standard library.
 */

/**
 * Alias to crec_snprintf.
 */
__attribute__((__format__(__printf__, 3, 4)))
__warn_unused_result __stdlib_compat int
snprintf(char *str, size_t size, const char *format, ...);

/**
 * Alias to crec_vsnprintf.
 */
__warn_unused_result __stdlib_compat int
vsnprintf(char *str, size_t size, const char *format, va_list args);

#endif /* __CROS_EC_STDIO_H__ */
