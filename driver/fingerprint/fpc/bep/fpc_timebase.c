/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* FPC Platform Abstraction Layer */

#include "common.h"
#include "fpc_timebase.h"
#include "timer.h"

#include <stdint.h>

__staticlib_hook uint32_t fpc_timebase_get_tick(void)
{
	clock_t time;

	time = clock();

	return (uint32_t)time;
}

__staticlib_hook void fpc_timebase_busy_wait(uint32_t ms)
{
	udelay(ms * 1000);
}
