/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* FPU module for Chrome EC operating system */

#include "task.h"

void enable_fpu(void)
{
	interrupt_disable();
	asm volatile("mrs r0, control;"
		     "orr r0, r0, #(1 << 2);"
		     "msr control, r0;"
		     "isb;");
}

void disable_fpu(int32_t v)
{
	/* Optimization barrier to force compiler generate floating point
	 * calculation code for 'v' before disabling FPU. */
	asm volatile("" : : "r" (v) : "memory");
	asm volatile("mrs r0, control;"
		     "bic r0, r0, #(1 << 2);"
		     "msr control, r0;"
		     "isb;");
	interrupt_enable();
}
