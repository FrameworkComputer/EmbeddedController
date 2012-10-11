/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* common.h - Common includes for Chrome EC */

#ifndef __CROS_EC_COMMON_H
#define __CROS_EC_COMMON_H

#include <stdint.h>

/*
 * Define __packed if someone hasn't beat us to it.  Linux kernel style
 * checking prefers __packed over __attribute__((packed)).
 */
#ifndef __packed
#define __packed __attribute__((packed))
#endif

/*
 * Include board and core configs, since those hold the CONFIG_ constants for a
 * given configuration.  This guarantees they get included everywhere, and
 * fixes a fairly common bug where we gate out code with #ifndef
 * CONFIG_SOMETHING and but forget to include both of these.
 */
#include "board.h"
#include "config.h"

/* List of common error codes that can be returned */
enum ec_error_list {
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
	/* Invalid argument */
	EC_ERROR_INVAL = 5,
	/* Already in use, or not ready yet */
	EC_ERROR_BUSY = 6,
	/* Access denied */
	EC_ERROR_ACCESS_DENIED = 7,
	/* Failed because component does not have power */
	EC_ERROR_NOT_POWERED = 8,
	/* Failed because component is not calibrated */
	EC_ERROR_NOT_CALIBRATED = 9,
	/* Invalid console command param (PARAMn means parameter n is bad) */
	EC_ERROR_PARAM1 = 11,
	EC_ERROR_PARAM2 = 12,
	EC_ERROR_PARAM3 = 13,
	EC_ERROR_PARAM4 = 14,
	EC_ERROR_PARAM5 = 15,
	EC_ERROR_PARAM6 = 16,
	EC_ERROR_PARAM7 = 17,
	EC_ERROR_PARAM8 = 18,
	EC_ERROR_PARAM9 = 19,
	EC_ERROR_PARAM_COUNT = 20,  /* Wrong number of params */

	/* Module-internal error codes may use this range.   */
	EC_ERROR_INTERNAL_FIRST = 0x10000,
	EC_ERROR_INTERNAL_LAST =  0x1FFFF
};

#endif  /* __CROS_EC_COMMON_H */
