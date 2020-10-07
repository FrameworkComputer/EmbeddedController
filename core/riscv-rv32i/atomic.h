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
	tmp;                                   \
})

/*
 * The atomic_* functions are marked as deprecated as a part of the process of
 * transaction to Zephyr compatible atomic functions. These prefixes will be
 * removed in the following patches. Please see b:169151160 for more details.
 */

static inline void deprecated_atomic_clear_bits(volatile uint32_t *addr,
						uint32_t bits)
{
	ATOMIC_OP(and, ~bits, addr);
}

static inline void deprecated_atomic_or(volatile uint32_t *addr, uint32_t bits)
{
	ATOMIC_OP(or, bits, addr);
}

static inline void deprecated_atomic_add(volatile uint32_t *addr,
					 uint32_t value)
{
	ATOMIC_OP(add, value, addr);
}

static inline void deprecated_atomic_sub(volatile uint32_t *addr,
					 uint32_t value)
{
	ATOMIC_OP(add, -value, addr);
}

static inline uint32_t deprecated_atomic_read_clear(volatile uint32_t *addr)
{
	return ATOMIC_OP(and, 0, addr);
}

static inline uint32_t deprecated_atomic_read_add(volatile uint32_t *addr,
						  uint32_t value)
{
	return ATOMIC_OP(add, value, addr);
}

static inline uint32_t deprecated_atomic_read_sub(volatile uint32_t *addr,
						  uint32_t value)
{
	return ATOMIC_OP(add, -value, addr);
}

#endif  /* __CROS_EC_ATOMIC_H */
