/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Atomic operations for Andes */

#ifndef __CROS_EC_ATOMIC_H
#define __CROS_EC_ATOMIC_H

#include "common.h"
#include "cpu.h"

static inline void atomic_clear(uint32_t volatile *addr, uint32_t bits)
{
	uint32_t psw = get_psw();
	asm volatile ("setgie.d");
	*addr &= ~bits;
	set_psw(psw);
}

static inline void atomic_or(uint32_t volatile *addr, uint32_t bits)
{
	uint32_t psw = get_psw();
	asm volatile ("setgie.d");
	*addr |= bits;
	set_psw(psw);
}

static inline void atomic_add(uint32_t volatile *addr, uint32_t value)
{
	uint32_t psw = get_psw();
	asm volatile ("setgie.d");
	*addr += value;
	set_psw(psw);
}

static inline void atomic_sub(uint32_t volatile *addr, uint32_t value)
{
	uint32_t psw = get_psw();
	asm volatile ("setgie.d");
	*addr -= value;
	set_psw(psw);
}

static inline uint32_t atomic_read_clear(uint32_t volatile *addr)
{
	uint32_t val;
	uint32_t psw = get_psw();
	asm volatile ("setgie.d");
	val = *addr;
	*addr = 0;
	set_psw(psw);
	return val;
}
#endif  /* __CROS_EC_ATOMIC_H */
