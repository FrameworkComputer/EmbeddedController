/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#if !defined(__CROS_EC_HOOKS_H) || defined(__CROS_EC_ZEPHYR_HOOKS_SHIM_H)
#error "This file must only be included from hooks.h. Include hooks.h directly."
#endif
#define __CROS_EC_ZEPHYR_HOOKS_SHIM_H

#include "common.h"
#include "cros_version.h"

#include <zephyr/init.h>
#include <zephyr/kernel.h>

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
		.work = &routine##_work_data,                        \
	}

/**
 * Record describing a single hook routine.
 */
struct zephyr_shim_hook_info {
	void (*routine)(void);
	uint16_t priority; /* HOOK_PRIO_LAST = 9999 */
};

/*
 * List of hook routines.
 *
 * Values are contiguous, where *start is the first and *(end - 1) is the last.
 */
struct zephyr_shim_hook_list {
	const struct zephyr_shim_hook_info *start;
	const struct zephyr_shim_hook_info *end;
};

/**
 * See include/hooks.h for documentation.
 */
#define DECLARE_HOOK(_hooktype, _routine, _priority)                       \
	BUILD_ASSERT(_hooktype == _hooktype, "Hook type is not defined."); \
	static const STRUCT_SECTION_ITERABLE_ALTERNATE(                    \
		zephyr_shim_hook_##_hooktype, zephyr_shim_hook_info,       \
		_cros_hook_##_hooktype##_##_routine) = {                   \
		.routine = _routine,                                       \
		.priority = _priority,                                     \
	}
