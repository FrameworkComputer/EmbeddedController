/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* FPC Platform Abstraction Layer */

#include <stdint.h>

#include "fpc_timebase.h"
#include "timer.h"

uint32_t __unused fpc_timebase_get_tick(void)
{
	clock_t time;

	time = clock();

	return (uint32_t)time;
}

void __unused fpc_timebase_busy_wait(uint32_t ms)
{
	udelay(ms * 1000);
}

void __unused fpc_timebase_init(void)
{
}
