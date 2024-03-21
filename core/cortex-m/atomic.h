/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Atomic operations for ARMv7 */

#ifndef __CROS_EC_ATOMIC_H
#define __CROS_EC_ATOMIC_H

#include "atomic_t.h"
#include "common.h"

#include <stdbool.h>

static inline atomic_val_t atomic_clear_bits(atomic_t *addr, atomic_val_t bits)
{
	return __atomic_fetch_and(addr, ~bits, __ATOMIC_SEQ_CST);
}

static inline atomic_val_t atomic_or(atomic_t *addr, atomic_val_t bits)
{
	return __atomic_fetch_or(addr, bits, __ATOMIC_SEQ_CST);
}

static inline atomic_val_t atomic_add(atomic_t *addr, atomic_val_t value)
{
	return __atomic_fetch_add(addr, value, __ATOMIC_SEQ_CST);
}

static inline atomic_val_t atomic_sub(atomic_t *addr, atomic_val_t value)
{
	return __atomic_fetch_sub(addr, value, __ATOMIC_SEQ_CST);
}

static inline atomic_val_t atomic_clear(atomic_t *addr)
{
	return __atomic_exchange_n(addr, 0, __ATOMIC_SEQ_CST);
}

static inline atomic_val_t atomic_and(atomic_t *addr, atomic_val_t bits)
{
	return __atomic_fetch_and(addr, bits, __ATOMIC_SEQ_CST);
}

static inline bool atomic_compare_exchange(atomic_t *addr,
					   atomic_val_t *expected,
					   atomic_val_t desired)
{
	return __atomic_compare_exchange_n(addr, expected, desired, false,
					   __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline atomic_val_t atomic_exchange(atomic_t *addr, atomic_val_t value)
{
	return __atomic_exchange_n(addr, value, __ATOMIC_SEQ_CST);
}

static inline atomic_val_t atomic_load(atomic_t *addr)
{
	return __atomic_load_n(addr, __ATOMIC_SEQ_CST);
}

#endif /* __CROS_EC_ATOMIC_H */
