/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#if !defined(__CROS_EC_HOOKS_H) || defined(__CROS_EC_ZEPHYR_HOOKS_SHIM_H)
#error "This file must only be included from hooks.h. Include hooks.h directly."
#endif
#define __CROS_EC_ZEPHYR_HOOKS_SHIM_H

#include <init.h>
#include <kernel.h>
#include <zephyr.h>

#include "common.h"
#include "cros_version.h"

/**
 * The internal data structure stored for a deferred function.
 */
struct deferred_data {
	struct k_work_delayable *work;
};

/**
 * See include/hooks.h for documentation.
 */
int hook_call_deferred(const struct deferred_data *data, int us);

#define DECLARE_DEFERRED(routine)                                    \
	K_WORK_DELAYABLE_DEFINE(routine##_work_data,                 \
				(void (*)(struct k_work *))routine); \
	__maybe_unused const struct deferred_data routine##_data = { \
		.work = &routine##_work_data,                \
	}

/**
 * Internal linked-list structure used to store hook lists.
 *
 * The info is constant, so storing a pointer to it in RAM rather than a copy
 * saves 4 bytes of RAM per entry (assuming 32-bit pointers and alignment).
 */
struct zephyr_shim_hook_info {
	void (*routine)(void);
	uint16_t priority; /* HOOK_PRIO_LAST = 9999 */
	enum hook_type type;
};

struct zephyr_shim_hook_list {
	const struct zephyr_shim_hook_info *info;
	struct zephyr_shim_hook_list *next;
};

/**
 * See include/hooks.h for documentation.
 */
#define DECLARE_HOOK(_hooktype, _routine, _priority)                     \
	static const struct zephyr_shim_hook_info                        \
		_cros_hook_##_hooktype##_info_##_routine = {             \
			.type = _hooktype,                               \
			.priority = _priority,                           \
			.routine = _routine,                             \
		};                                                       \
	STRUCT_SECTION_ITERABLE(zephyr_shim_hook_list,                   \
				_cros_hook_##_hooktype##_##_routine) = { \
		.info = &_cros_hook_##_hooktype##_info_##_routine,       \
	}
