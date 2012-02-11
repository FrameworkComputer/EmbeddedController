/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* FPU module for Chrome EC operating system */

#include "task.h"

void enable_fpu(void)
{
	interrupt_disable();
	asm("mrs r0, control\n"
	    "orr r0, r0, #(1 << 2)\n"
	    "msr control, r0\n"
	    "isb");
}

void disable_fpu(void)
{
	asm("mrs r0, control\n"
	    "bic r0, r0, #(1 << 2)\n"
	    "msr control, r0\n"
	    "isb");
	interrupt_enable();
}
