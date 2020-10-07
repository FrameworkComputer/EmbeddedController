/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Atomic operations for Andes */

#ifndef __CROS_EC_ATOMIC_H
#define __CROS_EC_ATOMIC_H

#include "common.h"
#include "cpu.h"
#include "task.h"

/*
 * The atomic_* functions are marked as deprecated as a part of the process of
 * transaction to Zephyr compatible atomic functions. These prefixes will be
 * removed in the following patches. Please see b:169151160 for more details.
 */

static inline void deprecated_atomic_clear_bits(uint32_t volatile *addr,
						uint32_t bits)
{
	uint32_t int_mask = read_clear_int_mask();

	*addr &= ~bits;
	set_int_mask(int_mask);
}

static inline void deprecated_atomic_or(uint32_t volatile *addr, uint32_t bits)
{
	uint32_t int_mask = read_clear_int_mask();

	*addr |= bits;
	set_int_mask(int_mask);
}

static inline void deprecated_atomic_add(uint32_t volatile *addr,
					 uint32_t value)
{
	uint32_t int_mask = read_clear_int_mask();

	*addr += value;
	set_int_mask(int_mask);
}

static inline void deprecated_atomic_sub(uint32_t volatile *addr,
					 uint32_t value)
{
	uint32_t int_mask = read_clear_int_mask();

	*addr -= value;
	set_int_mask(int_mask);
}

static inline uint32_t deprecated_atomic_read_clear(uint32_t volatile *addr)
{
	uint32_t val;
	uint32_t int_mask = read_clear_int_mask();

	val = *addr;
	*addr = 0;
	set_int_mask(int_mask);
	return val;
}
#endif  /* __CROS_EC_ATOMIC_H */
