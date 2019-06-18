/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Atomic operations for emulator */

#ifndef __CROS_EC_ATOMIC_H
#define __CROS_EC_ATOMIC_H

#include "common.h"

static inline void atomic_clear(uint32_t volatile *addr, uint32_t bits)
{
	__sync_and_and_fetch(addr, ~bits);
}

static inline void atomic_or(uint32_t volatile *addr, uint32_t bits)
{
	__sync_or_and_fetch(addr, bits);
}

static inline void atomic_add(uint32_t volatile *addr, uint32_t value)
{
	__sync_add_and_fetch(addr, value);
}

static inline void atomic_sub(uint32_t volatile *addr, uint32_t value)
{
	__sync_sub_and_fetch(addr, value);
}

static inline uint32_t atomic_read_clear(uint32_t volatile *addr)
{
	return __sync_fetch_and_and(addr, 0);
}
#endif  /* __CROS_EC_ATOMIC_H */
