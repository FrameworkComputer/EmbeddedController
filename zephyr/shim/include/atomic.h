/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ATOMIC_H
#define __CROS_EC_ATOMIC_H

#include <sys/atomic.h>

/*
 * Below EC APIs are being deprecated and replaced with the Zephyr
 * APIs.  We already get the Zephyr APIs from sys/atomic.h.  The
 * definitions here are provided so we can shim-in modules using the
 * deprecated APIs while the transition is under way.
 */
static inline void deprecated_atomic_clear_bits(uint32_t volatile *addr,
						uint32_t bits)
{
	atomic_and((atomic_t *)addr, bits);
}

static inline void deprecated_atomic_or(uint32_t volatile *addr, uint32_t bits)
{
	atomic_or((atomic_t *)addr, bits);
}

static inline void deprecated_atomic_add(uint32_t volatile *addr,
					 uint32_t value)
{
	atomic_add((atomic_t *)addr, value);
}

static inline void deprecated_atomic_sub(uint32_t volatile *addr,
					 uint32_t value)
{
	atomic_sub((atomic_t *)addr, value);
}

static inline uint32_t deprecated_atomic_read_clear(uint32_t volatile *addr)
{
	return atomic_clear((atomic_t *)addr);
}

#endif  /* __CROS_EC_ATOMIC_H */
