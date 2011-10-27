/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ec_common.h - Common includes for Chrome EC */

#ifndef __CROS_EC_COMMON_H
#define __CROS_EC_COMMON_H

#include <stdint.h>
#include <stdio.h>  /* FIXME: will be removed for portibility in the future */

/* Functions which return error return one of these.  This is an
 * integer instead of an enum to support module-internal error
 * codes. */
typedef int EcError;

/* List of common EcError codes that can be returned */
enum EcErrorList {
  /* Success - no error */
  EC_SUCCESS = 0,
  /* Unknown error */
  EC_ERROR_UNKNOWN = 1,
  /* Function not implemented yet */
  EC_ERROR_UNIMPLEMENTED = 2,
  /* Overflow error; too much input provided. */
  EC_ERROR_OVERFLOW = 3,
  /* Timeout */
  EC_ERROR_TIMEOUT = 4,
  /* Invalid parameter */
  EC_ERROR_INVALID_PARAMETER,
  /* Buffer is full, for output. */
  EC_ERROR_BUFFER_FULL,
  /* Buffer is empty, for input. */
  EC_ERROR_BUFFER_EMPTY,

  /* Module-internal error codes may use this range.   */
  EC_ERROR_INTERNAL_FIRST = 0x10000,
  EC_ERROR_INTERNAL_LAST =  0x1FFFF
};


/* TODO: move to a proper .h file */
#define PRINTF(fmt, ...) printf(fmt, __VA_ARGS__)

/* TODO: move to a proper .h file */
#define EC_ASSERT(expr) do { \
      if (!(expr)) {  \
        PRINTF("\n*** EC_ASSERT(%s) failed at file %s:%d.\n", \
               #expr, __FILE__, __LINE__);  \
        while (1);  \
      }  \
    } while (0)

#endif  /* __CROS_EC_COMMON_H */
