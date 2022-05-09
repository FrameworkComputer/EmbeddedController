/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_FPU_H
#define __CROS_EC_FPU_H

/*
 * These functions are available in newlib but we are are using Zephyr's
 * minimal library at present.
 *
 * This file is not called math.h to avoid a conflict with the toolchain's
 * built-in version.
 *
 * This code is taken from core/cortex-m/include/fpu.h
 */

#ifdef CONFIG_PLATFORM_EC_FPU

/* Implementation for Cortex-M */
#ifdef CONFIG_CPU_CORTEX_M
static inline float sqrtf(float v)
{
	float root;

	/* Use the CPU instruction */
	__asm__ volatile(
		"fsqrts %0, %1"
		: "=w" (root)
		: "w" (v)
	);

	return root;
}

static inline float fabsf(float v)
{
	float root;

	/* Use the CPU instruction */
	__asm__ volatile(
		"fabss %0, %1"
		: "=w" (root)
		: "w" (v)
	);

	return root;
}
#elif CONFIG_RISCV
static inline float sqrtf(float v)
{
	float root;

	__asm__("fsqrt.s %0, %1" : "=f"(root) : "f"(v));
	return root;
}

static inline float fabsf(float v)
{
	float abs;

	__asm__("fabs.s %0, %1" : "=f"(abs) : "f"(v));
	return abs;
}
#else
#error "Unsupported core: please add an implementation"
#endif

#endif  /* CONFIG_PLATFORM_EC_FPU */

#endif  /* __CROS_EC_MATH_H */
