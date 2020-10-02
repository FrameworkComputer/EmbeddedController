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

typedef int atomic_t;
typedef atomic_t atomic_val_t;

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

static inline void atomic_clear_bits(atomic_t *addr, atomic_val_t bits)
{
	__atomic_fetch_and(addr, ~bits, __ATOMIC_SEQ_CST);
}

static inline void deprecated_atomic_or(volatile uint32_t *addr, uint32_t bits)
{
	ATOMIC_OP(or, bits, addr);
}

static inline atomic_val_t atomic_or(atomic_t *addr, atomic_val_t bits)
{
	return __atomic_fetch_or(addr, bits, __ATOMIC_SEQ_CST);
}

static inline void deprecated_atomic_add(volatile uint32_t *addr,
					 uint32_t value)
{
	ATOMIC_OP(add, value, addr);
}

static inline atomic_val_t atomic_add(atomic_t *addr, atomic_val_t value)
{
	return __atomic_fetch_add(addr, value, __ATOMIC_SEQ_CST);
}

static inline void deprecated_atomic_sub(volatile uint32_t *addr,
					 uint32_t value)
{
	ATOMIC_OP(add, -value, addr);
}

static inline atomic_val_t atomic_sub(atomic_t *addr, atomic_val_t value)
{
	return __atomic_fetch_sub(addr, value, __ATOMIC_SEQ_CST);
}

static inline uint32_t deprecated_atomic_read_clear(volatile uint32_t *addr)
{
	return ATOMIC_OP(and, 0, addr);
}

static inline atomic_val_t atomic_read_clear(atomic_t *addr)
{
	return __atomic_exchange_n(addr, 0, __ATOMIC_SEQ_CST);
}

static inline uint32_t deprecated_atomic_read_add(volatile uint32_t *addr,
						  uint32_t value)
{
	return ATOMIC_OP(add, value, addr);
}

static inline atomic_val_t atomic_read_add(atomic_t *addr, atomic_val_t value)
{
	return __atomic_fetch_add(addr, value, __ATOMIC_SEQ_CST);
}

static inline uint32_t deprecated_atomic_read_sub(volatile uint32_t *addr,
						  uint32_t value)
{
	return ATOMIC_OP(add, -value, addr);
}

static inline atomic_val_t atomic_read_sub(atomic_t *addr, atomic_val_t value)
{
	return __atomic_fetch_sub(addr, value, __ATOMIC_SEQ_CST);
}

#endif  /* __CROS_EC_ATOMIC_H */
