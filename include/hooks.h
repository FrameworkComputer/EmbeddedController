/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System hooks for Chrome EC */

#ifndef __CROS_EC_HOOKS_H
#define __CROS_EC_HOOKS_H

#include "common.h"

enum hook_priority {
	HOOK_PRIO_FIRST = 1,       /* Highest priority */
	HOOK_PRIO_DEFAULT = 5000,  /* Default priority */
	HOOK_PRIO_LAST = 9999      /* Lowest priority */
};


enum hook_type {
	HOOK_INIT = 0,     /* System init */
	HOOK_FREQ_CHANGE,  /* System clock changed frequency */
	HOOK_SYSJUMP,      /* About to jump to another image.  Modules which
			    * need to preserve data across such a jump should
			    * save it here and restore it in HOOK_INIT.
			    *
			    * NOTE: This hook is called with interrupts
			    * disabled! */
};


struct hook_data {
	/* Hook processing routine; returns EC error code. */
	int (*routine)(void);
	/* Priority; low numbers = higher priority. */
	int priority;
};


/* Call all the hook routines of a specified type.  If stop_on_error, stops on
 * the first non-EC_SUCCESS return code.  Returns the first non-EC_SUCCESS
 * return code, if any, or EC_SUCCESS if all hooks returned EC_SUCCESS. */
int hook_notify(enum hook_type type, int stop_on_error);


/* Register a hook routine.  <hooktype> should be one of enum hook_type.
 * <routine> should be int routine(void), and should return an error code or
 * EC_SUCCESS if no error.  <priority> should be between HOOK_PRIO_FIRST and
 * HOOK_PRIO_LAST, and should be HOOK_PRIO_DEFAULT unless there's a compelling
 * reason to care about the order in which hooks are called. */
#define DECLARE_HOOK(hooktype, routine, priority)			\
	const struct hook_data __hook_##hooktype##_##routine		\
	__attribute__((section(".rodata." #hooktype)))			\
	     = {routine, priority}

#endif  /* __CROS_EC_HOOKS_H */
