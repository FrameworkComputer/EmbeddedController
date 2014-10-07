/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "hwtimer.h"

/* TODO(crosbug.com/p/33432): Implement these functions */

uint32_t __hw_clock_event_get(void)
{
	return 0;
}

uint32_t __hw_clock_source_read(void)
{
	return 0;
}

int __hw_clock_source_init(uint32_t start_t)
{
	return 0;
}
