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

typedef int atomic_t;
typedef atomic_t atomic_val_t;

static inline atomic_val_t atomic_clear_bits(atomic_t *addr, atomic_val_t bits)
{
	atomic_val_t ret;
	atomic_t volatile *ptr = addr;
	uint32_t int_mask = read_clear_int_mask();

	ret = *ptr;
	*ptr &= ~bits;
	set_int_mask(int_mask);
	return ret;
}

static inline atomic_val_t atomic_or(atomic_t *addr, atomic_val_t bits)
{
	atomic_val_t ret;
	atomic_t volatile *ptr = addr;
	uint32_t int_mask = read_clear_int_mask();

	ret = *ptr;
	*ptr |= bits;
	set_int_mask(int_mask);
	return ret;
}

static inline atomic_val_t atomic_add(atomic_t *addr, atomic_val_t value)
{
	atomic_val_t ret;
	atomic_t volatile *ptr = addr;
	uint32_t int_mask = read_clear_int_mask();

	ret = *ptr;
	*ptr += value;
	set_int_mask(int_mask);
	return ret;
}

static inline atomic_val_t atomic_sub(atomic_t *addr, atomic_val_t value)
{
	atomic_val_t ret;
	atomic_t volatile *ptr = addr;
	uint32_t int_mask = read_clear_int_mask();

	ret = *ptr;
	*ptr -= value;
	set_int_mask(int_mask);
	return ret;
}

static inline atomic_val_t atomic_clear(atomic_t *addr)
{
	atomic_val_t ret;
	atomic_t volatile *ptr = addr;
	uint32_t int_mask = read_clear_int_mask();

	ret = *ptr;
	*ptr = 0;
	set_int_mask(int_mask);
	return ret;
}

#endif  /* __CROS_EC_ATOMIC_H */
