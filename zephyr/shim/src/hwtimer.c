/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <stdint.h>

#include "hwtimer.h"

uint64_t __hw_clock_source_read64(void)
{
	return k_ticks_to_us_floor64(k_uptime_ticks());
}

uint32_t __hw_clock_event_get(void)
{
	/*
	 * CrOS EC event deadlines don't quite make sense in Zephyr
	 * terms.  Evaluate what to do about this later...
	 */
	return 0;
}

void udelay(unsigned us)
{
	k_busy_wait(us);
}
