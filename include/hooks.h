/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System hooks for Chrome EC */

#ifndef __CROS_EC_HOOKS_H
#define __CROS_EC_HOOKS_H

#include "common.h"

enum hook_priority {
	/* Generic values across all hooks */
	HOOK_PRIO_FIRST = 1,       /* Highest priority */
	HOOK_PRIO_DEFAULT = 5000,  /* Default priority */
	HOOK_PRIO_LAST = 9999,     /* Lowest priority */

	/* Specific hook vales for HOOK_INIT */
	/* LPC inits before modules which need memory-mapped I/O */
	HOOK_PRIO_INIT_LPC = HOOK_PRIO_FIRST + 1,
	/* Chipset inits before modules which need to know its initial state. */
	HOOK_PRIO_INIT_CHIPSET = HOOK_PRIO_FIRST + 2,
};

enum hook_type {
	/*
	 * System initialization.
	 *
	 * Hook routines are called from main(), after all hard-coded inits,
	 * before task scheduling is enabled.
	 */
	HOOK_INIT = 0,

	/*
	 * System clock changed frequency.
	 *
	 * Hook routines are called from the context which initiates the
	 * frequency change.
	 */
	HOOK_FREQ_CHANGE,

	/*
	 * About to jump to another image.  Modules which need to preserve data
	 * across such a jump should save it here and restore it in HOOK_INIT.
	 *
	 * Hook routines are called from the context which initiates the jump,
	 * WITH INTERRUPTS DISABLED.
	 */
	HOOK_SYSJUMP,

	/*
	 * Initialization for components such as PMU to be done before host
	 * chipset/AP starts up.
	 *
	 * Hook routines are called from the chipset task.
	 */
	HOOK_CHIPSET_PRE_INIT,

	/* System is starting up.  All suspend rails are now on.
	 *
	 * Hook routines are called from the chipset task.
	 */
	HOOK_CHIPSET_STARTUP,

	/*
	 * System is resuming from suspend, or booting and has reached the
	 * point where all voltage rails are on.
	 *
	 * Hook routines are called from the chipset task.
	 */
	HOOK_CHIPSET_RESUME,

	/*
	 * System is suspending, or shutting down; all voltage rails are still
	 * on.
	 *
	 * Hook routines are called from the chipset task.
	 */
	HOOK_CHIPSET_SUSPEND,

	/*
	 * System is shutting down.  All suspend rails are still on.
	 *
	 * Hook routines are called from the chipset task.
	 */
	HOOK_CHIPSET_SHUTDOWN,

	/*
	 * AC power plugged in or removed.
	 *
	 * Hook routines are called from the TICK task.
	 */
	HOOK_AC_CHANGE,

	/*
	 * Lid opened or closed.  Based on debounced lid state, not raw lid
	 * GPIO input.
	 *
	 * Hook routines are called from the chipset task.
	 */
	HOOK_LID_CHANGE,

	/*
	 * Periodic tick, every HOOK_TICK_INTERVAL.
	 *
	 * Hook routines will be called from the TICK task.
	 */
	HOOK_TICK,

	/*
	 * Periodic tick, every second.
	 *
	 * Hook routines will be called from the TICK task.
	 */
	HOOK_SECOND,
};

struct hook_data {
	/* Hook processing routine. */
	void (*routine)(void);
	/* Priority; low numbers = higher priority. */
	int priority;
};

/**
 * Call all the hook routines of a specified type.
 *
 * @param type		Type of hook routines to call.
 */
void hook_notify(enum hook_type type);

/**
 * Register a hook routine.
 *
 * @param hooktype	Type of hook for routine (enum hook_type)
 * @param routine	Hook routine, with prototype void routine(void)
 * @param priority      Priority for determining when routine is called vs.
 *			other hook routines; should be between HOOK_PRIO_FIRST
 *                      and HOOK_PRIO_LAST, and should be HOOK_PRIO_DEFAULT
 *			unless there's a compelling reason to care about the
 *			order in which hooks are called.
 */
#define DECLARE_HOOK(hooktype, routine, priority)			\
	const struct hook_data __hook_##hooktype##_##routine		\
	__attribute__((section(".rodata." #hooktype)))			\
	     = {routine, priority}

#endif  /* __CROS_EC_HOOKS_H */
