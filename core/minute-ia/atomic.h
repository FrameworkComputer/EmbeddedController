/* Copyright (c) 2016 The Chromium OS Authors. All rights reserved.
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
		"lock;" #asm_op " %0, %1\n"	\
		: 				\
		: "r" (v), "m" (*a)		\
		: "memory");			\
} while (0)

inline int bool_compare_and_swap_u32(uint32_t *var, uint32_t old_value,
		uint32_t new_value);
inline void atomic_or_u8(uint8_t *var, uint8_t value);
inline void atomic_and_u8(uint8_t *var, uint8_t value);
inline void atomic_clear(uint32_t volatile *addr, uint32_t bits);
inline void atomic_or(uint32_t volatile *addr, uint32_t bits);
inline void atomic_add(uint32_t volatile *addr, uint32_t value);
inline void atomic_and(uint32_t volatile *addr, uint32_t value);
inline void atomic_sub(uint32_t volatile *addr, uint32_t value);
inline uint32_t atomic_read_clear(uint32_t volatile *addr);

#endif  /* __CROS_EC_ATOMIC_H */
