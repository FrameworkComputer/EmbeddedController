/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ATOMIC_BIT_H
#define __CROS_EC_ATOMIC_BIT_H

#ifndef CONFIG_ZEPHYR
#include "atomic.h"

#ifdef __cplusplus
extern "C" {
#endif
static inline atomic_val_t atomic_get(const atomic_t *target)
{
	return __atomic_load_n(target, __ATOMIC_SEQ_CST);
}
#ifdef __cplusplus
}
#endif

#include "third_party/zephyr/atomic.h"
#endif /* CONFIG_ZEPHYR */
#endif /* __CROS_EC_ATOMIC_BIT_H */
