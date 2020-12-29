/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cpu.h"
#include "panic.h"

#ifdef CONFIG_FPU
/* Single precision floating point square root. */
float sqrtf(float x)
{
	asm volatile (
		"fsqrt.s %0, %1"
		: "=f" (x)
		: "f" (x));

	return x;
}
#endif

void __ubsan_handle_divrem_overflow(void *data,
				    void *lhs, void *rhs)
{
	exception_panic(PANIC_SW_DIV_ZERO, 0);
}
