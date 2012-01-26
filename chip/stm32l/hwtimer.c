/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hardware timers driver */

#include <stdint.h>

#include "board.h"
#include "hwtimer.h"
#include "task.h"

void __hw_clock_event_set(uint32_t deadline)
{
}

uint32_t __hw_clock_event_get(void)
{
	return 0;
}

void __hw_clock_event_clear(void)
{
}

uint32_t __hw_clock_source_read(void)
{
	return 0;
}

int __hw_clock_source_init(void)
{
	return -1;
}
