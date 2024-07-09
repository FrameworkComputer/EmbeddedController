/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#ifndef __CROS_EC_ATOMIC_H
#define __CROS_EC_ATOMIC_H

#include <zephyr/sys/atomic.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline atomic_val_t atomic_clear_bits(atomic_t *addr, atomic_val_t bits)
{
	return atomic_and(addr, ~bits);
}

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_ATOMIC_H */
