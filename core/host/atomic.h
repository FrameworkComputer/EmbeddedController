/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Atomic operations for emulator */

#ifndef __CROS_EC_ATOMIC_H
#define __CROS_EC_ATOMIC_H

#include "atomic_t.h"
#include "common.h"

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
#endif /* __CROS_EC_ATOMIC_H */
