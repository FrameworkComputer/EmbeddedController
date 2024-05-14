/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_SHIMMED_TASK_ID_H
#define __CROS_EC_SHIMMED_TASK_ID_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Task identifier (8 bits) */
typedef uint8_t task_id_t;

/*
 * Bitmask of port enable bits, expanding to a value like `BIT(0) | BIT(2) | 0`.
 */
#define PD_INT_SHARED_PORT_MASK                                           \
	(FOR_EACH_NONEMPTY_TERM(                                          \
		BIT, (|),                                                 \
		IF_ENABLED(CONFIG_PLATFORM_EC_USB_PD_PORT_0_SHARED, (0)), \
		IF_ENABLED(CONFIG_PLATFORM_EC_USB_PD_PORT_1_SHARED, (1)), \
		IF_ENABLED(CONFIG_PLATFORM_EC_USB_PD_PORT_2_SHARED, (2)), \
		IF_ENABLED(CONFIG_PLATFORM_EC_USB_PD_PORT_3_SHARED, (3)), ) 0)

/* Highest priority on bottom -- same as in platform/ec. */
enum {
	EC_TASK_PRIO_LOWEST = 0,
	EC_SYSWORKQ_PRIO = EC_TASK_PRIO_LOWEST,
	EC_TASK_RWSIG_PRIO,
	EC_TASK_TOUCHPAD_PRIO,
	EC_TASK_FPSENSOR_PRIO,
	EC_TASK_CHG_RAMP_PRIO,
	EC_TASK_USB_CHG_PRIO,
	EC_TASK_DPS_PRIO,
	EC_TASK_CHARGER_PRIO,
	EC_TASK_CEC_PRIO,
	EC_TASK_CHIPSET_PRIO,
	EC_TASK_MOTIONSENSE_PRIO,
	EC_TASK_USB_MUX_PRIO,
	EC_TASK_HOSTCMD_PRIO,
	EC_SHELL_PRIO,
	EC_TASK_KEYPROTO_PRIO,
	EC_TASK_POWERBTN_PRIO,
	EC_TASK_KEYSCAN_PRIO,
	EC_TASK_PD_C0_PRIO,
	EC_TASK_PD_C1_PRIO,
	EC_TASK_PD_C2_PRIO,
	EC_TASK_PD_C3_PRIO,
	EC_TASK_PD_INT_SHARED_PRIO,
	EC_TASK_PD_INT_C0_PRIO,
	EC_TASK_PD_INT_C1_PRIO,
	EC_TASK_PD_INT_C2_PRIO,
	EC_TASK_PD_INT_C3_PRIO,
	EC_TASK_PRIO_COUNT,
};

/* Helper macro to set tasks priorities */
#define EC_TASK_PRIORITY(prio) K_PRIO_PREEMPT(EC_TASK_PRIO_COUNT - prio - 1)

/*
 * List of CROS_EC_TASK items. See CONFIG_TASK_LIST in platform/ec's config.h
 * for more information.  For tests that want their own custom tasks, use
 * CONFIG_HAS_TEST_TASKS and not CONFIG_SHIMMED_TASKS.
 */
#ifdef CONFIG_SHIMMED_TASKS
#define CROS_EC_TASK_LIST                                                  \
	COND_CODE_1(HAS_TASK_CHG_RAMP,                                     \
		    (CROS_EC_TASK(CHG_RAMP, chg_ramp_task, 0,              \
				  CONFIG_TASK_CHG_RAMP_STACK_SIZE,         \
				  EC_TASK_CHG_RAMP_PRIO, 0)),              \
		    ())                                                    \
	COND_CODE_1(CONFIG_PLATFORM_EC_USB_CHARGER,                        \
		    (CROS_EC_TASK(USB_CHG, usb_charger_task_shared, 0,     \
				  CONFIG_TASK_USB_CHG_STACK_SIZE,          \
				  EC_TASK_USB_CHG_PRIO, 0)),               \
		    ())                                                    \
	COND_CODE_1(HAS_TASK_DPS,                                          \
		    (CROS_EC_TASK(DPS, dps_task, 0,                        \
				  CONFIG_TASK_DPS_STACK_SIZE,              \
				  EC_TASK_DPS_PRIO, 0)),                   \
		    ())                                                    \
	COND_CODE_1(HAS_TASK_CHARGER,                                      \
		    (CROS_EC_TASK(CHARGER, charger_task, 0,                \
				  CONFIG_TASK_CHARGER_STACK_SIZE,          \
				  EC_TASK_CHARGER_PRIO, 0)),               \
		    ())                                                    \
	COND_CODE_1(HAS_TASK_CHIPSET,                                      \
		    (CROS_EC_TASK(CHIPSET, chipset_task, 0,                \
				  CONFIG_TASK_CHIPSET_STACK_SIZE,          \
				  EC_TASK_CHIPSET_PRIO, 0)),               \
		    ())                                                    \
	COND_CODE_1(HAS_TASK_MOTIONSENSE,                                  \
		    (CROS_EC_TASK(MOTIONSENSE, motion_sense_task, 0,       \
				  CONFIG_TASK_MOTIONSENSE_STACK_SIZE,      \
				  EC_TASK_MOTIONSENSE_PRIO, 0)),           \
		    ())                                                    \
	IF_ENABLED(HAS_TASK_USB_MUX,                                       \
		   (CROS_EC_TASK(USB_MUX, usb_mux_task, 0,                 \
				 CONFIG_TASK_USB_MUX_STACK_SIZE,           \
				 EC_TASK_USB_MUX_PRIO, 0)))                \
	COND_CODE_1(HAS_TASK_HOSTCMD_DEDICATED,                            \
		    (CROS_EC_TASK(HOSTCMD, host_command_task, 0,           \
				  CONFIG_TASK_HOSTCMD_STACK_SIZE,          \
				  EC_TASK_HOSTCMD_PRIO, 0)),               \
		    ())                                                    \
	COND_CODE_1(HAS_TASK_KEYPROTO,                                     \
		    (CROS_EC_TASK(KEYPROTO, keyboard_protocol_task, 0,     \
				  CONFIG_TASK_KEYPROTO_STACK_SIZE,         \
				  EC_TASK_KEYPROTO_PRIO, 0)),              \
		    ())                                                    \
	COND_CODE_1(HAS_TASK_POWERBTN,                                     \
		    (CROS_EC_TASK(POWERBTN, power_button_task, 0,          \
				  CONFIG_TASK_POWERBTN_STACK_SIZE,         \
				  EC_TASK_POWERBTN_PRIO, 0)),              \
		    ())                                                    \
	COND_CODE_1(HAS_TASK_KEYSCAN,                                      \
		    (CROS_EC_TASK(KEYSCAN, keyboard_scan_task, 0,          \
				  CONFIG_TASK_KEYSCAN_STACK_SIZE,          \
				  EC_TASK_KEYSCAN_PRIO, 0)),               \
		    ())                                                    \
	COND_CODE_1(HAS_TASK_PD_C0,                                        \
		    (CROS_EC_TASK(PD_C0, pd_task, 0,                       \
				  CONFIG_TASK_PD_STACK_SIZE,               \
				  EC_TASK_PD_C0_PRIO, 0)),                 \
		    ())                                                    \
	COND_CODE_1(HAS_TASK_PD_C1,                                        \
		    (CROS_EC_TASK(PD_C1, pd_task, 0,                       \
				  CONFIG_TASK_PD_STACK_SIZE,               \
				  EC_TASK_PD_C1_PRIO, 0)),                 \
		    ())                                                    \
	COND_CODE_1(HAS_TASK_PD_C2,                                        \
		    (CROS_EC_TASK(PD_C2, pd_task, 0,                       \
				  CONFIG_TASK_PD_STACK_SIZE,               \
				  EC_TASK_PD_C2_PRIO, 0)),                 \
		    ())                                                    \
	COND_CODE_1(HAS_TASK_PD_C3,                                        \
		    (CROS_EC_TASK(PD_C3, pd_task, 0,                       \
				  CONFIG_TASK_PD_STACK_SIZE,               \
				  EC_TASK_PD_C3_PRIO, 0)),                 \
		    ())                                                    \
	IF_ENABLED(CONFIG_HAS_TASK_PD_INT_SHARED,                          \
		   (CROS_EC_TASK(PD_INT_SHARED, pd_shared_alert_task,      \
				 PD_INT_SHARED_PORT_MASK,                  \
				 CONFIG_TASK_PD_INT_STACK_SIZE,            \
				 EC_TASK_PD_INT_SHARED_PRIO, 0)))          \
	COND_CODE_1(HAS_TASK_PD_INT_C0,                                    \
		    (CROS_EC_TASK(PD_INT_C0, pd_interrupt_handler_task, 0, \
				  CONFIG_TASK_PD_INT_STACK_SIZE,           \
				  EC_TASK_PD_INT_C0_PRIO, 0)),             \
		    ())                                                    \
	COND_CODE_1(HAS_TASK_PD_INT_C1,                                    \
		    (CROS_EC_TASK(PD_INT_C1, pd_interrupt_handler_task, 1, \
				  CONFIG_TASK_PD_INT_STACK_SIZE,           \
				  EC_TASK_PD_INT_C1_PRIO, 0)),             \
		    ())                                                    \
	COND_CODE_1(HAS_TASK_PD_INT_C2,                                    \
		    (CROS_EC_TASK(PD_INT_C2, pd_interrupt_handler_task, 2, \
				  CONFIG_TASK_PD_INT_STACK_SIZE,           \
				  EC_TASK_PD_INT_C2_PRIO, 0)),             \
		    ())                                                    \
	COND_CODE_1(HAS_TASK_PD_INT_C3,                                    \
		    (CROS_EC_TASK(PD_INT_C3, pd_interrupt_handler_task, 3, \
				  CONFIG_TASK_PD_INT_STACK_SIZE,           \
				  EC_TASK_PD_INT_C3_PRIO, 0)),             \
		    ())                                                    \
	COND_CODE_1(HAS_TASK_TOUCHPAD,                                     \
		    (CROS_EC_TASK(TOUCHPAD, touchpad_task, 0,              \
				  CONFIG_TASK_TOUCHPAD_STACK_SIZE,         \
				  EC_TASK_TOUCHPAD_PRIO, 0)),              \
		    ())                                                    \
	COND_CODE_1(HAS_TASK_FPSENSOR,                                     \
		    (CROS_EC_TASK(FPSENSOR, fp_task, 0,                    \
				  CONFIG_TASK_FPSENSOR_STACK_SIZE,         \
				  EC_TASK_FPSENSOR_PRIO, K_FP_REGS)),      \
		    ())                                                    \
	COND_CODE_1(HAS_TASK_RWSIG,                                        \
		    (CROS_EC_TASK(RWSIG, rwsig_task, 0,                    \
				  CONFIG_TASK_RWSIG_STACK_SIZE,            \
				  EC_TASK_RWSIG_PRIO, 0)),                 \
		    ())                                                    \
	COND_CODE_1(HAS_TASK_CEC,                                          \
		    (CROS_EC_TASK(CEC, cec_task, 0,                        \
				  CONFIG_TASK_CEC_STACK_SIZE,              \
				  EC_TASK_CEC_PRIO, 0)),                   \
		    ())
#elif defined(CONFIG_HAS_TEST_TASKS)
#include "shimmed_test_tasks.h"
/*
 * There are two different ways to define a task list (because historical
 * reasons). Applications use CROS_EC_TASK_LIST to define their tasks, while
 * unit tests that need additional tasks use CONFIG_TEST_TASK_LIST. For
 * shimming a unit test, define CROS_EC_TASK_LIST as whatever
 * CONFIG_TEST_TASK_LIST expands to.
 */
#if defined(CONFIG_TEST_TASK_LIST) && !defined(CROS_EC_TASK_LIST)
#define CROS_EC_TASK_LIST CONFIG_TEST_TASK_LIST
#endif /* CONFIG_TEST_TASK_LIST && !CROS_EC_TASK_LIST */

/*
 * Tests often must link in files that reference task IDs, even when the
 * shimmed tasks are not created.  Define stub tasks to satisfy the final link.
 */
#else /* !CONFIG_SHIMMED_TASKS && !CONFIG_HAS_TEST_TASKS */
#define CROS_EC_TASK_LIST                                                      \
	CROS_EC_TASK(CHG_RAMP, NULL, 0, 0, EC_TASK_CHG_RAMP_PRIO, 0)           \
	CROS_EC_TASK(USB_CHG, NULL, 0, 0, EC_TASK_USB_CHG_PRIO, 0)             \
	CROS_EC_TASK(DPS, NULL, 0, 0, EC_TASK_DPS_PRIO, 0)                     \
	CROS_EC_TASK(CHARGER, NULL, 0, 0, EC_TASK_CHARGER_PRIO, 0)             \
	CROS_EC_TASK(CHIPSET, NULL, 0, 0, EC_TASK_CHIPSET_PRIO, 0)             \
	CROS_EC_TASK(MOTIONSENSE, NULL, 0, 0, EC_TASK_MOTIONSENSE_PRIO, 0)     \
	CROS_EC_TASK(USB_MUX, NULL, 0, 0, EC_TASK_USB_MUX_PRIO, 0)             \
	CROS_EC_TASK(HOSTCMD, NULL, 0, 0, EC_TASK_HOSTCMD_PRIO, 0)             \
	CROS_EC_TASK(KEYPROTO, NULL, 0, 0, EC_TASK_KEYPROTO_PRIO, 0)           \
	CROS_EC_TASK(POWERBTN, NULL, 0, 0, EC_TASK_POWERBTN_PRIO, 0)           \
	CROS_EC_TASK(KEYSCAN, NULL, 0, 0, EC_TASK_KEYSCAN_PRIO, 0)             \
	CROS_EC_TASK(PD_C0, NULL, 0, 0, EC_TASK_PD_C0_PRIO, 0)                 \
	CROS_EC_TASK(PD_C1, NULL, 0, 0, EC_TASK_PD_C1_PRIO, 0)                 \
	CROS_EC_TASK(PD_C2, NULL, 0, 0, EC_TASK_PD_C2_PRIO, 0)                 \
	CROS_EC_TASK(PD_C3, NULL, 0, 0, EC_TASK_PD_C3_PRIO, 0)                 \
	CROS_EC_TASK(PD_INT_SHARED, NULL, 0, 0, EC_TASK_PD_INT_SHARED_PRIO, 0) \
	CROS_EC_TASK(PD_INT_C0, NULL, 0, 0, EC_TASK_PD_INT_C0_PRIO, 0)         \
	CROS_EC_TASK(PD_INT_C1, NULL, 1, 0, EC_TASK_PD_INT_C1_PRIO, 0)         \
	CROS_EC_TASK(PD_INT_C2, NULL, 2, 0, EC_TASK_PD_INT_C2_PRIO, 0)         \
	CROS_EC_TASK(PD_INT_C3, NULL, 3, 0, EC_TASK_PD_INT_C3_PRIO, 0)         \
	CROS_EC_TASK(TOUCHPAD, NULL, 0, 0, EC_TASK_TOUCHPAD_PRIO, 0)           \
	CROS_EC_TASK(FPSENSOR, NULL, 0, 0, EC_TASK_FPSENSOR_PRIO, K_FP_REGS)   \
	CROS_EC_TASK(CEC, NULL, 0, 0, EC_TASK_CEC_PRIO, 0)

#endif /* CONFIG_SHIMMED_TASKS */

#ifndef CROS_EC_TASK_LIST
#define CROS_EC_TASK_LIST
#endif /* CROS_EC_TASK_LIST */

/*
 * Define the task_ids globally for all shimmed platform/ec code to use.
 * Note that unit test task lists use TASK_TEST, which we can just alias
 * into a regular CROS_EC_TASK.
 */
#define CROS_EC_TASK(name, ...) TASK_ID_##name,
#define TASK_TEST(name, ...) CROS_EC_TASK(name)
/* clang-format off */
enum {
	CROS_EC_TASK_LIST
#ifdef TEST_BUILD
	TASK_ID_TEST_RUNNER,
#endif
	TASK_ID_COUNT,
	TASK_ID_INVALID = 0xff, /* Unable to find the task */
};
/* clang-format on */
#undef CROS_EC_TASK
#undef TASK_TEST

/*
 * Additional task IDs for features that runs on non shimmed threads,
 * thread_id_to_task_id() and task_id_to_thread_id() need to be updated
 * to identify these tasks.
 */
/* clang-format off */
#define CROS_EC_EXTRA_TASKS(fn)                                         \
	COND_CODE_1(CONFIG_TASK_HOSTCMD_THREAD_MAIN, (fn(HOSTCMD)),     \
		(fn(MAIN)))                                             \
	COND_CODE_1(CONFIG_TASK_HOSTCMD_THREAD_DEDICATED,               \
		(IF_ENABLED(CONFIG_EC_HOST_CMD, (fn(HOSTCMD)))), ())    \
	COND_CODE_1(CONFIG_SHELL_BACKEND_SERIAL, (fn(SHELL)),           \
		(COND_CODE_1(CONFIG_SHELL_BACKEND_DUMMY, (fn(SHELL)),   \
		())))							\
	fn(SYSWORKQ)                                                    \
	fn(IDLE)
/* clang-format on */

#define EXTRA_TASK_INTERNAL_ID(name) EXTRA_TASK_##name,
enum {
	CROS_EC_EXTRA_TASKS(EXTRA_TASK_INTERNAL_ID) EXTRA_TASK_COUNT,
};

#define EXTRA_TASK_ID(name) \
	TASK_ID_##name = ((int)TASK_ID_COUNT + (int)EXTRA_TASK_##name),
enum { CROS_EC_EXTRA_TASKS(EXTRA_TASK_ID) };

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_SHIMMED_TASK_ID_H */
