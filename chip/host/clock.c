/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Mock clock driver for unit test.
 */

#include "clock.h"

int clock_get_freq(void)
{
	return 16000000;
}
