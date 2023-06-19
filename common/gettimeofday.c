/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gettimeofday.h"
#include "timer.h"

#include <stddef.h>

enum ec_error_list ec_gettimeofday(struct timeval *restrict tv,
				   void *restrict tz)
{
	uint64_t now;

	if (tv == NULL)
		return EC_ERROR_INVAL;

	now = get_time().val;
	tv->tv_sec = now / SECOND;
	tv->tv_usec = now % SECOND;
	return EC_SUCCESS;
}
