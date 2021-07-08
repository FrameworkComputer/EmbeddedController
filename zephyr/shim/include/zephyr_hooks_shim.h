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
#if IS_ZEPHYR_VERSION(2, 6)
	struct k_work_delayable *work;
#else
	struct k_delayed_work *work;
#endif
};

/**
 * See include/hooks.h for documentation.
 */
int hook_call_deferred(const struct deferred_data *data, int us);

#if IS_ZEPHYR_VERSION(2, 6)
#define DECLARE_DEFERRED(routine)                                    \
	K_WORK_DELAYABLE_DEFINE(routine##_work_data,                 \
				(void (*)(struct k_work *))routine); \
	__maybe_unused const struct deferred_data routine##_data = { \
		.work = &routine##_work_data,                \
	}
#else
#define DECLARE_DEFERRED(routine)                                    \
	K_DELAYED_WORK_DEFINE(routine##_work_data,                   \
			      (void (*)(struct k_work *))routine);   \
	__maybe_unused const struct deferred_data routine##_data = { \
		.work = &routine##_work_data,                \
	}
#endif

/**
 * Internal linked-list structure used to store hook lists.
 */
struct zephyr_shim_hook_list {
	void (*routine)(void);
	int priority;
	enum hook_type type;
	struct zephyr_shim_hook_list *next;
};

/**
 * See include/hooks.h for documentation.
 */
#define DECLARE_HOOK(_hooktype, _routine, _priority)             \
	STRUCT_SECTION_ITERABLE(zephyr_shim_hook_list,           \
			_cros_hook_##_hooktype##_##_routine) = { \
		.type = _hooktype,                               \
		.routine = _routine,                             \
		.priority = _priority,                           \
	}
