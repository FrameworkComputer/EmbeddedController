/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Atomic operations for x86 */

#ifndef __CROS_EC_ATOMIC_H
#define __CROS_EC_ATOMIC_H

#include "common.h"
#include "util.h"

#define ATOMIC_OP(asm_op, a, v) do {		\
	__asm__ __volatile__ (			\
		ASM_LOCK_PREFIX #asm_op " %1, %0\n"	\
		: "+m" (*a)			\
		: "ir" (v)			\
		: "memory");			\
} while (0)

static inline int bool_compare_and_swap_u32(uint32_t *var, uint32_t old_value,
		uint32_t new_value)
{
	uint32_t _old_value = old_value;

	__asm__ __volatile__(ASM_LOCK_PREFIX "cmpxchgl %2, %1"
			     : "=a" (old_value), "+m" (*var)
			     : "r" (new_value), "0" (old_value)
			     : "memory");

	return (_old_value == old_value);
}

/*
 * The atomic_* functions are marked as deprecated as a part of the process of
 * transaction to Zephyr compatible atomic functions. These prefixes will be
 * removed in the following patches. Please see b:169151160 for more details.
 */

static inline void deprecated_atomic_or_u8(uint8_t *addr, uint8_t bits)
{
	ATOMIC_OP(or, addr, bits);
}

static inline void deprecated_atomic_and_u8(uint8_t *addr, uint8_t bits)
{
	ATOMIC_OP(and, addr, bits);
}

static inline void deprecated_atomic_clear_bits(uint32_t volatile *addr,
						uint32_t bits)
{
	ATOMIC_OP(andl, addr, ~bits);
}

static inline void deprecated_atomic_or(uint32_t volatile *addr, uint32_t bits)
{
	ATOMIC_OP(orl, addr, bits);
}

static inline void deprecated_atomic_add(uint32_t volatile *addr,
					 uint32_t value)
{
	ATOMIC_OP(addl, addr, value);
}

static inline void deprecated_atomic_and(uint32_t volatile *addr,
					 uint32_t value)
{
	ATOMIC_OP(andl, addr, value);
}

static inline void deprecated_atomic_sub(uint32_t volatile *addr,
					 uint32_t value)
{
	ATOMIC_OP(subl, addr, value);
}

static inline uint32_t deprecated_atomic_read_clear(uint32_t volatile *addr)
{
	int ret = 0;

	if (*addr == 0)
		return 0;

	asm volatile(ASM_LOCK_PREFIX "xchgl %0, %1\n"
		     : "+r" (ret), "+m" (*addr)
		     : : "memory", "cc");

	return ret;
}

#endif  /* __CROS_EC_ATOMIC_H */
