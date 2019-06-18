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

static inline void atomic_clear(uint32_t volatile *addr, uint32_t bits)
{
	uint32_t int_mask = get_int_mask();
	interrupt_disable();
	*addr &= ~bits;
	set_int_mask(int_mask);
}

static inline void atomic_or(uint32_t volatile *addr, uint32_t bits)
{
	uint32_t int_mask = get_int_mask();
	interrupt_disable();
	*addr |= bits;
	set_int_mask(int_mask);
}

static inline void atomic_add(uint32_t volatile *addr, uint32_t value)
{
	uint32_t int_mask = get_int_mask();
	interrupt_disable();
	*addr += value;
	set_int_mask(int_mask);
}

static inline void atomic_sub(uint32_t volatile *addr, uint32_t value)
{
	uint32_t int_mask = get_int_mask();
	interrupt_disable();
	*addr -= value;
	set_int_mask(int_mask);
}

static inline uint32_t atomic_read_clear(uint32_t volatile *addr)
{
	uint32_t val;
	uint32_t int_mask = get_int_mask();
	interrupt_disable();
	val = *addr;
	*addr = 0;
	set_int_mask(int_mask);
	return val;
}
#endif  /* __CROS_EC_ATOMIC_H */
