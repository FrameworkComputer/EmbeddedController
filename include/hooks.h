/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System hooks for Chrome EC */

#ifndef __CROS_EC_HOOKS_H
#define __CROS_EC_HOOKS_H

#include "common.h"

enum hook_priority {
	/* Generic values across all hooks */
	HOOK_PRIO_FIRST = 1, /* Highest priority */
	HOOK_PRIO_POST_FIRST = HOOK_PRIO_FIRST + 1,
	HOOK_PRIO_DEFAULT = 5000, /* Default priority */
	HOOK_PRIO_PRE_DEFAULT = HOOK_PRIO_DEFAULT - 1,
	HOOK_PRIO_POST_DEFAULT = HOOK_PRIO_DEFAULT + 1,
	HOOK_PRIO_LAST = 9999, /* Lowest priority */

	/* Specific hook vales for HOOK_INIT */
	/* DMA inits before ADC, I2C, SPI */
	HOOK_PRIO_INIT_DMA = HOOK_PRIO_FIRST + 1,
	/* LPC inits before modules which need memory-mapped I/O */
	HOOK_PRIO_INIT_LPC = HOOK_PRIO_FIRST + 1,
	/*
	 * I2C dependents (battery, sensors, etc), everything but the
	 * controllers. I2C controller is now initialized in main.c
	 * TODO(b/138384267): Split this hook up and name the resulting
	 * ones more semantically.
	 */
	HOOK_PRIO_INIT_I2C = HOOK_PRIO_FIRST + 2,
	HOOK_PRIO_PRE_I2C = HOOK_PRIO_INIT_I2C - 1,
	HOOK_PRIO_POST_I2C = HOOK_PRIO_INIT_I2C + 1,
	HOOK_PRIO_BATTERY_INIT = HOOK_PRIO_POST_I2C,
	HOOK_PRIO_POST_BATTERY_INIT = HOOK_PRIO_BATTERY_INIT + 1,
	/* Chipset inits before modules which need to know its initial state. */
	HOOK_PRIO_INIT_CHIPSET = HOOK_PRIO_FIRST + 3,
	HOOK_PRIO_POST_CHIPSET = HOOK_PRIO_INIT_CHIPSET + 1,
	/* Lid switch inits before power button */
	HOOK_PRIO_INIT_LID = HOOK_PRIO_FIRST + 4,
	HOOK_PRIO_POST_LID = HOOK_PRIO_INIT_LID + 1,
	/* Power button inits before chipset and switch */
	HOOK_PRIO_INIT_POWER_BUTTON = HOOK_PRIO_FIRST + 5,
	HOOK_PRIO_POST_POWER_BUTTON = HOOK_PRIO_INIT_POWER_BUTTON + 1,
	/* Init switch states after power button / lid */
	HOOK_PRIO_INIT_SWITCH = HOOK_PRIO_FIRST + 6,
	/* Init fan before PWM */
	HOOK_PRIO_INIT_FAN = HOOK_PRIO_FIRST + 7,
	/* PWM inits before modules which might use it (LEDs) */
	HOOK_PRIO_INIT_PWM = HOOK_PRIO_FIRST + 8,
	HOOK_PRIO_POST_PWM = HOOK_PRIO_INIT_PWM + 1,
	/* SPI inits before modules which might use it (sensors) */
	HOOK_PRIO_INIT_SPI = HOOK_PRIO_FIRST + 9,
	/* Extpower inits before modules which might use it (battery, LEDs) */
	HOOK_PRIO_INIT_EXTPOWER = HOOK_PRIO_FIRST + 10,
	/* Init VBOOT hash later, since it depends on deferred functions */
	HOOK_PRIO_INIT_VBOOT_HASH = HOOK_PRIO_FIRST + 11,
	/* Init charge manager before usage in board init */
	HOOK_PRIO_INIT_CHARGE_MANAGER = HOOK_PRIO_FIRST + 12,
	HOOK_PRIO_POST_CHARGE_MANAGER = HOOK_PRIO_INIT_CHARGE_MANAGER + 1,

	HOOK_PRIO_INIT_ADC = HOOK_PRIO_DEFAULT,
	HOOK_PRIO_INIT_DAC = HOOK_PRIO_DEFAULT,

	/* Specific values to lump temperature-related hooks together */
	HOOK_PRIO_TEMP_SENSOR = 6000,
	/* After all sensors have been polled */
	HOOK_PRIO_TEMP_SENSOR_DONE = HOOK_PRIO_TEMP_SENSOR + 1,
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
	 * The "pre" frequency hook is called before we change the frequency.
	 * There is no way to cancel.  Hook routines are always called from
	 * a task, so it's OK to lock a mutex here.  However, they may be called
	 * from a deferred task on some platforms so callbacks must make sure
	 * not to do anything that would require some other deferred task to
	 * run.
	 */
	HOOK_PRE_FREQ_CHANGE,
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

#ifdef CONFIG_CHIPSET_RESUME_INIT_HOOK
	/*
	 * Initialization before the system resumes, like enabling the SPI
	 * driver such that it can receive a host resume event.
	 *
	 * Hook routines are called from the chipset task.
	 */
	HOOK_CHIPSET_RESUME_INIT,

	/*
	 * System has suspended. It is paired with CHIPSET_RESUME_INIT hook,
	 * like reverting the initialization of the SPI driver.
	 *
	 * Hook routines are called from the chipset task.
	 */
	HOOK_CHIPSET_SUSPEND_COMPLETE,
#endif

	/*
	 * System is shutting down.  All suspend rails are still on.
	 *
	 * Hook routines are called from the chipset task.
	 */
	HOOK_CHIPSET_SHUTDOWN,

	/*
	 * System has already shut down. All the suspend rails are already off.
	 *
	 * Hook routines are called from the chipset task.
	 */
	HOOK_CHIPSET_SHUTDOWN_COMPLETE,

	/*
	 * System is in G3.  All power rails are now turned off.
	 *
	 * Hook routines are called from the chipset task.
	 */
	HOOK_CHIPSET_HARD_OFF,

	/*
	 * System reset in S0.  All rails are still up.
	 *
	 * Hook routines are called from the chipset task.
	 */
	HOOK_CHIPSET_RESET,

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
	 * Hook routines are called from the TICK task.
	 */
	HOOK_LID_CHANGE,

	/*
	 * Device in tablet mode (base behind lid).
	 *
	 * Hook routines are called from the TICK task.
	 */
	HOOK_TABLET_MODE_CHANGE,

#ifdef CONFIG_BODY_DETECTION
	/*
	 * Body dectection mode change.
	 *
	 * Hook routines are called from the HOOKS task.
	 */
	HOOK_BODY_DETECT_CHANGE,
#endif

	/*
	 * Detachable device connected to a base.
	 *
	 * Hook routines are called from the TICK task.
	 */
	HOOK_BASE_ATTACHED_CHANGE,

	/*
	 * Power button pressed or released.  Based on debounced power button
	 * state, not raw GPIO input.
	 *
	 * Hook routines are called from the TICK task.
	 */
	HOOK_POWER_BUTTON_CHANGE,

	/*
	 * Battery state-of-charge changed
	 *
	 * Hook routines are called from the charger task.
	 */
	HOOK_BATTERY_SOC_CHANGE,

#ifdef CONFIG_USB_SUSPEND
	/*
	 * Called when there is a change in USB power management status
	 * (suspended or resumed).
	 *
	 * Hook routines are called from HOOKS task.
	 */
	HOOK_USB_PM_CHANGE,
#endif

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

	/*
	 * USB PD cc disconnect event.
	 */
	HOOK_USB_PD_DISCONNECT,

	/*
	 * USB PD cc connection event.
	 */
	HOOK_USB_PD_CONNECT,

	/*
	 * Power supply change event.
	 */
	HOOK_POWER_SUPPLY_CHANGE,

#ifdef TEST_BUILD
	/*
	 * Special hook types to be used by unit tests of the hooks
	 * implementation only.
	 */
	HOOK_TEST_1,
	HOOK_TEST_2,
	HOOK_TEST_3,
#endif /* TEST_BUILD */

	/*
	 * Not a hook type (instead the number of hooks). This should
	 * always be placed at the end of this enumeration.
	 */
	HOOK_TYPE_COUNT,
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
 * This function must be called from the correct type-specific context (task);
 * see enum hook_type for details.  hook_notify() should NEVER be called from
 * interrupt context unless specifically allowed for a hook type, because hook
 * routines may need to perform task-level calls like usleep() and mutex
 * operations that are not valid in interrupt context.  Instead of calling a
 * hook from interrupt context, use a deferred function.
 *
 * @param type		Type of hook routines to call.
 */
void hook_notify(enum hook_type type);

/*
 * CONFIG_PLATFORM_EC_HOOKS is enabled by default during a Zephyr
 * build, but can be disabled via Kconfig if desired (leaving the stub
 * implementation at the bottom of this file).
 */
#if defined(CONFIG_PLATFORM_EC_HOOKS)
#include "zephyr_hooks_shim.h"
#elif defined(CONFIG_COMMON_RUNTIME)
struct deferred_data {
	/* Deferred function pointer */
	void (*routine)(void);
};

/**
 * Start a timer to call a deferred routine.
 *
 * The routine will be called after at least the specified delay, in the
 * context of the hook task.
 *
 * @param data	The deferred_data struct created by invoking DECLARE_DEFERRED().
 * @param us	Delay in microseconds until routine will be called.  If the
 *		routine is already pending, subsequent calls will change the
 *		delay.  Pass us=0 to call as soon as possible, or -1 to cancel
 *		the deferred call.
 *
 * @return non-zero if error.
 */
int hook_call_deferred(const struct deferred_data *data, int us);

/**
 * Register a hook routine.
 *
 * NOTE: Hook routines must be careful not to leave resources locked which may
 * be needed by other hook routines or deferred function calls.  This can cause
 * a deadlock, because most hooks and all deferred functions are called from
 * the same hook task.  For example:
 *
 *   hook1(): lock foo
 *   deferred1(): lock foo, use foo, unlock foo
 *   hook2(): unlock foo
 *
 * In this example, hook1() and hook2() lock and unlock a shared resource foo
 * (for example, a mutex).  If deferred1() attempts to lock the resource, it
 * will stall waiting for the resource to be unlocked.  But the unlock will
 * never happen, because hook2() won't be called by the hook task until
 * deferred1() returns.
 *
 * @param hooktype	Type of hook for routine (enum hook_type)
 * @param routine	Hook routine, with prototype void routine(void)
 * @param priority      Priority for determining when routine is called vs.
 *			other hook routines; should be between HOOK_PRIO_FIRST
 *                      and HOOK_PRIO_LAST, and should be HOOK_PRIO_DEFAULT
 *			unless there's a compelling reason to care about the
 *			order in which hooks are called.
 */
#define DECLARE_HOOK(hooktype, routine, priority)                            \
	const struct hook_data __keep __no_sanitize_address CONCAT4(         \
		__hook_, hooktype, _, routine)                               \
		__attribute__((section(".rodata." STRINGIFY(hooktype)))) = { \
			routine, priority                                    \
		}

/**
 * Register a deferred function call.
 *
 * DECLARE_DEFERRED creates a new deferred_data struct with a name constructed
 * by concatenating _data to the name of the routine passed.
 *
 * To call a deferred routine defined as:
 *     DECLARE_DEFERRED(foo)
 * You would call
 *     hook_call_deferred(&foo_data, delay_in_microseconds);
 *
 * NOTE: Deferred function call routines must be careful not to leave resources
 * locked which may be needed by other hook routines or deferred function
 * calls.  This can cause a deadlock, because most hooks and all deferred
 * functions are called from the same hook task.  See DECLARE_HOOK() for an
 * example.
 *
 * @param routine	Function pointer, with prototype void routine(void)
 */
#define DECLARE_DEFERRED(routine)                                        \
	const struct deferred_data __keep __no_sanitize_address CONCAT2( \
		routine, _data)                                          \
		__attribute__((section(".rodata.deferred"))) = { routine }
#else
/*
 * Stub implementation in case hooks are disabled (neither
 * CONFIG_COMMON_RUNTIME nor CONFIG_PLATFORM_EC_HOOKS is defined)
 */
#define hook_call_deferred(unused1, unused2) -1
#define DECLARE_HOOK(t, func, p)                     \
	void CONCAT4(unused_hook_, t, _, func)(void) \
	{                                            \
		func();                              \
	}
#define DECLARE_DEFERRED(func)                     \
	void CONCAT2(unused_deferred_, func)(void) \
	{                                          \
		func();                            \
	}
#endif

#endif /* __CROS_EC_HOOKS_H */
