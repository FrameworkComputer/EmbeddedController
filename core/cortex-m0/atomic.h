/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Atomic operations for ARMv6-M */

#ifndef __CROS_EC_ATOMIC_H
#define __CROS_EC_ATOMIC_H

#include "atomic_t.h"
#include "common.h"

/**
 * Implements atomic arithmetic operations on 32-bit integers.
 *
 * There is no load/store exclusive on ARMv6-M, just disable interrupts
 */
#define ATOMIC_OP(asm_op, a, v)                                 \
	({                                                      \
		uint32_t reg0, reg1;                            \
                                                                \
		__asm__ __volatile__(".syntax unified\n"        \
				     "   cpsid i\n"             \
				     "   ldr  %0, [%2]\n"       \
				     "   mov  %1, %0\n" #asm_op \
				     " %0, %0, %3\n"            \
				     "   str  %0, [%2]\n"       \
				     "   cpsie i\n"             \
				     : "=&l"(reg0), "=&l"(reg1) \
				     : "l"(a), "r"(v)           \
				     : "cc", "memory");         \
		reg1;                                           \
	})

static inline atomic_val_t atomic_clear_bits(atomic_t *addr, atomic_val_t bits)
{
	return ATOMIC_OP(bics, addr, bits);
}

static inline atomic_val_t atomic_or(atomic_t *addr, atomic_val_t bits)
{
	return ATOMIC_OP(orrs, addr, bits);
}

static inline atomic_val_t atomic_add(atomic_t *addr, atomic_val_t value)
{
	return ATOMIC_OP(add, addr, value);
}

static inline atomic_val_t atomic_sub(atomic_t *addr, atomic_val_t value)
{
	return ATOMIC_OP(subs, addr, value);
}

static inline atomic_val_t atomic_clear(atomic_t *addr)
{
	atomic_t ret;

	__asm__ __volatile__("   movs    %2, #0\n"
			     "   cpsid   i\n"
			     "   ldr     %0, [%1]\n"
			     "   str     %2, [%1]\n"
			     "   cpsie   i\n"
			     : "=&l"(ret)
			     : "l"(addr), "r"(0)
			     : "cc", "memory");

	return ret;
}

static inline atomic_val_t atomic_and(atomic_t *addr, atomic_val_t bits)
{
	return ATOMIC_OP(ands, addr, bits);
}

#endif /* __CROS_EC_ATOMIC_H */
