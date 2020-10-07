/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Atomic operations for ARMv7 */

#ifndef __CROS_EC_ATOMIC_H
#define __CROS_EC_ATOMIC_H

#include "common.h"

/**
 * Implements atomic arithmetic operations on 32-bit integers.
 *
 * It used load/store exclusive.
 * If you write directly the integer used as an atomic variable,
 * you must either clear explicitly the exclusive monitor (using clrex)
 * or do it in exception context (which clears the monitor).
 */
#define ATOMIC_OP(asm_op, a, v) do {				\
	uint32_t reg0, reg1;                                    \
								\
	__asm__ __volatile__("1: ldrex   %0, [%2]\n"            \
			     #asm_op" %0, %0, %3\n"		\
			     "   strex   %1, %0, [%2]\n"        \
			     "   teq     %1, #0\n"              \
			     "   bne     1b"                    \
			     : "=&r" (reg0), "=&r" (reg1)       \
			     : "r" (a), "r" (v) : "cc");        \
} while (0)

/*
 * The atomic_* functions are marked as deprecated as a part of the process of
 * transaction to Zephyr compatible atomic functions. These prefixes will be
 * removed in the following patches. Please see b:169151160 for more details.
 */

static inline void deprecated_atomic_clear_bits(uint32_t volatile *addr,
						uint32_t bits)
{
	ATOMIC_OP(bic, addr, bits);
}

static inline void deprecated_atomic_or(uint32_t volatile *addr, uint32_t bits)
{
	ATOMIC_OP(orr, addr, bits);
}

static inline void deprecated_atomic_add(uint32_t volatile *addr,
					 uint32_t value)
{
	ATOMIC_OP(add, addr, value);
}

static inline void deprecated_atomic_sub(uint32_t volatile *addr,
					 uint32_t value)
{
	ATOMIC_OP(sub, addr, value);
}

static inline uint32_t deprecated_atomic_read_clear(uint32_t volatile *addr)
{
	uint32_t ret, tmp;

	__asm__ __volatile__("   mov     %3, #0\n"
			     "1: ldrex   %0, [%2]\n"
			     "   strex   %1, %3, [%2]\n"
			     "   teq     %1, #0\n"
			     "   bne     1b"
			     : "=&r" (ret), "=&r" (tmp)
			     : "r" (addr), "r" (0) : "cc");

	return ret;
}
#endif  /* __CROS_EC_ATOMIC_H */
