/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_GETTIMEOFDAY_H
#define __CROS_EC_GETTIMEOFDAY_H

#include "common.h"

#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get the time.
 *
 * @warning This does not match gettimeofday() exactly; it does not return the
 * time since the Unix Epoch.
 *
 * @param[out] tv time
 * @param[in] tz ignored
 * @return EC_SUCCESS on success
 * @return EC_ERROR_INVAL on invalid parameters
 */
enum ec_error_list ec_gettimeofday(struct timeval *restrict tv,
				   void *restrict tz);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_GETTIMEOFDAY_H */
