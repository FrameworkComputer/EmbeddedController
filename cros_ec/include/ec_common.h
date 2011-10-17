/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ec_common.h - Common includes for Chrome EC */

#ifndef __CROS_EC_COMMON_H
#define __CROS_EC_COMMON_H

#include <stdint.h>

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

  /* Module-internal error codes may use this range.   */
  EC_ERROR_INTERNAL_FIRST = 0x10000,
  EC_ERROR_INTERNAL_LAST =  0x1FFFF
};

#endif  /* __CROS_EC_COMMON_H */
