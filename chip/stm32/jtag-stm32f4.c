/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Settings to enable JTAG debugging */

#include "jtag.h"
#include "registers.h"

void jtag_pre_init(void)
{
	/*
	 * Stop all timers we might use (TIM1-8) and watchdogs when
	 * the JTAG stops the CPU.
	 */
	/* TODO(nsanders): Implement this if someone needs jtag. */
}
