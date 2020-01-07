/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"

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
