/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

void clock_init(void)
{
}

int clock_get_freq(void)
{
	/* constant 16 MHz clock */
	return 16000000;
}
