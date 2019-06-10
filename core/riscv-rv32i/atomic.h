/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Atomic operations for RISC_V */

#ifndef __CROS_EC_ATOMIC_H
#define __CROS_EC_ATOMIC_H

#include "common.h"
#include "cpu.h"
#include "task.h"

#define ATOMIC_OP(op, value, addr)             \
({                                             \
	uint32_t tmp;                          \
	asm volatile (                         \
		"amo" #op ".w.aqrl %0, %2, %1" \
		: "=r" (tmp), "+A" (*addr)     \
		: "r" (value));                \
})

static inline void atomic_clear(volatile uint32_t *addr, uint32_t bits)
{
	ATOMIC_OP(and, ~bits, addr);
}

static inline void atomic_or(volatile uint32_t *addr, uint32_t bits)
{
	ATOMIC_OP(or, bits, addr);
}

static inline void atomic_add(volatile uint32_t *addr, uint32_t value)
{
	ATOMIC_OP(add, value, addr);
}

static inline void atomic_sub(volatile uint32_t *addr, uint32_t value)
{
	ATOMIC_OP(add, -value, addr);
}

static inline uint32_t atomic_read_clear(volatile uint32_t *addr)
{
	uint32_t ret;

	asm volatile (
		"amoand.w.aqrl  %0, %2, %1"
		: "=r" (ret), "+A" (*addr)
		: "r" (0));

	return ret;
}
#endif  /* __CROS_EC_ATOMIC_H */
