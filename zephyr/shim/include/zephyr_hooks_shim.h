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

/**
 * The internal data structure stored for a deferred function.
 */
struct deferred_data {
	void (*routine)(void);
	struct k_delayed_work delayed_work;
};

/**
 * See include/hooks.h for documentation.
 */
int hook_call_deferred(struct deferred_data *data, int us);

/**
 * Runtime helper to setup deferred data.
 *
 * @param data		The struct deferred_data.
 */
void zephyr_shim_setup_deferred(struct deferred_data *data);

/**
 * See include/hooks.h for documentation.
 */
#define DECLARE_DEFERRED(routine) _DECLARE_DEFERRED(routine)
#define _DECLARE_DEFERRED(_routine)                                        \
	static __maybe_unused struct deferred_data _routine##_data = {     \
		.routine = _routine,                                       \
	};                                                                 \
	static int _setup_deferred_##_routine(const struct device *unused) \
	{                                                                  \
		ARG_UNUSED(unused);                                        \
		zephyr_shim_setup_deferred(&_routine##_data);              \
		return 0;                                                  \
	}                                                                  \
	SYS_INIT(_setup_deferred_##_routine, APPLICATION, 1)

/**
 * Internal linked-list structure used to store hook lists.
 */
struct zephyr_shim_hook_list {
	void (*routine)(void);
	int priority;
	struct zephyr_shim_hook_list *next;
};

/**
 * Runtime helper for DECLARE_HOOK setup data.
 *
 * @param type		The type of hook.
 * @param routine	The handler for the hook.
 * @param priority	The priority (smaller values are executed first).
 * @param entry		A statically allocated list entry.
 */
void zephyr_shim_setup_hook(enum hook_type type, void (*routine)(void),
			    int priority, struct zephyr_shim_hook_list *entry);

/**
 * See include/hooks.h for documentation.
 */
#define DECLARE_HOOK(hooktype, routine, priority) \
	_DECLARE_HOOK_1(hooktype, routine, priority, __LINE__)
#define _DECLARE_HOOK_1(hooktype, routine, priority, line) \
	_DECLARE_HOOK_2(hooktype, routine, priority, line)
#define _DECLARE_HOOK_2(hooktype, routine, priority, line)                 \
	static int _setup_hook_##line(const struct device *unused)         \
	{                                                                  \
		ARG_UNUSED(unused);                                        \
		static struct zephyr_shim_hook_list lst;                   \
		zephyr_shim_setup_hook(hooktype, routine, priority, &lst); \
		return 0;                                                  \
	}                                                                  \
	SYS_INIT(_setup_hook_##line, APPLICATION, 1)
