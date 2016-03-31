/* Copyright (c) 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Math utility functions for minute-IA */

#ifndef __CROS_EC_MATH_H
#define __CROS_EC_MATH_H

#ifdef CONFIG_FPU
static inline float sqrtf(float v)
{
	float root;

	asm volatile(
		"fsqrt %0, %1"
		: "=w" (root)
		: "w" (v)
	);
	return root;
}

static inline float fabsf(float v)
{
	float root;

	asm volatile(
		"fabs %0, %1"
		: "=w" (root)
		: "w" (v)
	);
	return root;
}
#endif  /* CONFIG_FPU */

#endif  /* __CROS_EC_MATH_H */
