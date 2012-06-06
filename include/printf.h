/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Printf-like functionality for Chrome EC */

#ifndef __CROS_EC_PRINTF_H
#define __CROS_EC_PRINTF_H

#include <stdarg.h>  /* For va_list */
#include "common.h"

/* SUPPORTED FORMAT CODES:
 *   char (%c)
 *   string (%s)
 *   native int (signed/unsigned) (%d / %u / %x / %X)
 *   int32_t / uint32_t (%d / %x / %X)
 *   int64_t / uint64_t (%ld / %lu / %lx / %lX)
 *   pointer (%p)
 * And the following special format codes:
 *   current time in sec (%T) - interpreted as "%.6T" for fixed-point format
 * including padding (%-5s, %8d, %08x, %016lx)
 *
 * Floating point output (%f / %g) is not supported, but there is a fixed-point
 * extension for integers; a padding option of .N (where N is a number) will
 * put a decimal before that many digits.  For example, printing 123 with
 * format code %.6d will result in "0.000123".  This is most useful for
 * printing times, voltages, and currents. */


/* Print formatted output to a function, like vfprintf()
 *
 * addchar() will be called for every character to be printed, with the context
 * pointer passed to vfnprintf().  addchar() should return 0 if the character
 * was accepted or non-zero if the character was dropped due to overflow.
 *
 * Returns error if output was truncated. */
int vfnprintf(int (*addchar)(void *context, int c), void *context,
	      const char *format, va_list args);


/* Print formatted outut to a string */
int snprintf(char *str, int size, const char *format, ...);


#endif  /* __CROS_EC_PRINTF_H */
