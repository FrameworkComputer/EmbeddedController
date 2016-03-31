/* Copyright (c) 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"

inline int bool_compare_and_swap_u32(uint32_t *var, uint32_t old_value,
		uint32_t new_value)
{
	uint32_t _old_value = old_value;

	__asm__ __volatile__("cmpxchg %1, %2\n"
			: "=a"(old_value)
			: "r"(new_value), "m"(*var), "a" (old_value)
			: "memory");

	return (_old_value == old_value);
}

inline void atomic_or_u8(uint8_t *addr, uint8_t bits)
{
	ATOMIC_OP(or, addr, bits);
}

inline void atomic_and_u8(uint8_t *addr, uint8_t bits)
{
	ATOMIC_OP(and, addr, bits);
}

inline void atomic_clear(uint32_t volatile *addr, uint32_t bits)
{
	ATOMIC_OP(btr, addr, bits >> 1);
}

inline void atomic_or(uint32_t volatile *addr, uint32_t bits)
{
	ATOMIC_OP(orl, addr, bits);
}

inline void atomic_add(uint32_t volatile *addr, uint32_t value)
{
	ATOMIC_OP(addl, addr, value);
}

inline void atomic_and(uint32_t volatile *addr, uint32_t value)
{
	ATOMIC_OP(andl, addr, value);
}

inline void atomic_sub(uint32_t volatile *addr, uint32_t value)
{
	ATOMIC_OP(subl, addr, value);
}

inline uint32_t atomic_read_clear(uint32_t volatile *addr)
{
	int loc = 0;

	if (*addr == 0)
		return 0;

	asm volatile("bsr %1, %0\n"
		     "lock; btr %0, %1\n"
			: "=&r" (loc)
			: "m" (*addr)
			: "memory"
		    );

	return (1 << loc);
}
