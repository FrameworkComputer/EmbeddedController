/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Atomic operations for ARMv6-M */

#ifndef __CROS_EC_ATOMIC_H
#define __CROS_EC_ATOMIC_H

#include "common.h"

/**
 * Implements atomic arithmetic operations on 32-bit integers.
 *
 * There is no load/store exclusive on ARMv6-M, just disable interrupts
 */
#define ATOMIC_OP(asm_op, a, v) do {				\
	uint32_t reg0;						\
								\
	__asm__ __volatile__("   cpsid i\n"			\
			     "   ldr  %0, [%1]\n"		\
			     #asm_op" %0, %0, %2\n"		\
			     "   str  %0, [%1]\n"		\
			     "   cpsie i\n"			\
			     : "=&r" (reg0)			\
			     : "b" (a), "r" (v) : "cc");	\
} while (0)

static inline void atomic_clear(uint32_t *addr, uint32_t bits)
{
	ATOMIC_OP(bic, addr, bits);
}

static inline void atomic_or(uint32_t *addr, uint32_t bits)
{
	ATOMIC_OP(orr, addr, bits);
}

static inline void atomic_add(uint32_t *addr, uint32_t value)
{
	ATOMIC_OP(add, addr, value);
}

static inline void atomic_sub(uint32_t *addr, uint32_t value)
{
	ATOMIC_OP(sub, addr, value);
}

static inline uint32_t atomic_read_clear(uint32_t *addr)
{
	uint32_t ret;

	__asm__ __volatile__("   mov     %2, #0\n"
			     "   cpsid   i\n"
			     "   ldr     %0, [%1]\n"
			     "   str     %2, [%1]\n"
			     "   cpsie   i\n"
			     : "=&r" (ret)
			     : "b" (addr), "r" (0) : "cc");

	return ret;
}
#endif  /* __CROS_EC_ATOMIC_H */
