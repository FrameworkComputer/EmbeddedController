/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _AP_PWRSEQ_SM_H_
#define _AP_PWRSEQ_SM_H_
#include "ap_power/ap_pwrseq_sm_defs.h"

/**
 * AP power sequence state machine API
 * -----------------------------------
 *
 * State machine is integrated into the AP power sequence driver by wrapping
 * Zephyr State Machine Framework (SMF), each SMF state is represented by three
 * functions or action handlers that define operations performed on state entry,
 * run and exit.
 *
 * ACPI’s global state (G3) and its six sleep power states (S0, S1, S2, S3, S4,
 * S5) are present within this state machine domain. All these ACPI states are
 * are divided in three levels, each level is a SMF state with a hierarchical
 * relation with others action handlers of the same ACPI state, state handlers
 * at higher levels performes the most common task of corresponding ACPI power
 * states.
 *
 * Architecture is the highest level of the hierarchy, SMF states this level
 * must do operations that are specific to AP CPU architecture, example:
 * X86 (intel), ARM.
 *
 * Middle level is the chipset; these SMF action handlers carry out operations
 * to drive power of components that are required for the AP chip. Any bus
 * signal or internal power rail that is vital for chip execution is a good fit
 * to be handled in these action handlers. Examples of chipsets are: Tiger Lake
 * and Jasper Lake, these are Intel chipsets that use X86 architecture, and
 * MT8186 and MT8192 are Mediatek chipsets using ARM architecture.
 *
 * Application is the bottom level of the hierarchy and these SMF action
 * handlers are reserved to address board or application specific computations.
 *
 * Hierarchical SMF will coordinate execution of entry, run & exit functions
 * accordingly. Given that implementation is responsible for doing state
 * transitions, the following considerations should be taken when implementing
 * action state handlers:
 *
 * - Higher level `entry` actions are executed before the lower level `entry`
 * actions.
 * - Transitioning from one substate to another with a shared upper level state
 * does not re-execute the upper level `entry` action or execute the `exit`
 * action.
 * - Upper level `exit` actions are executed after the substate `exit` actions.
 * - Lower level `run` actions are executed before upper level.
 * - Upper level `run` actions only executes if no state transition has been
 * made from lower level `run` action.
 *
 * Please refer to Zephyr SMF documentation.
 *
 * This file exports macros that help to provide action handlers implementation
 * for all states, and any substate that is declared in devicetree. It also
 * declares functions to do power state transitions.
 *
 * Macros AP_POWER_ACRH_STATE_DEFINE, AP_POWER_CHIPSET_STATE_DEFINE and
 * AP_POWER_APP_STATE_DEFINE statically declare action handlers for each power
 * state.
 *
 * State Machine Workflow
 * ----------------------
 *
 * State machine execution is done within AP power sequence driver thread
 * context. Driver sets initial state upon initialization.
 *
 * On each driver thread loop ieration, current state `run` action handler is
 * called following hierarchical order as set in zephyr SMF.
 *
 * State machine implements Ultimate Hook pattern, this allows upper level
 * action handlers to finalize hierarchical execution flow by setting its
 * returning value to anything different than zero.
 *
 * `ap_pwrseq_sm_set_state` must be used to do state transition, this will
 * execute current state `exit` action handlers followed by next state `entry`
 * action handlers, completing state transtion on next thread loop iteration
 * when new state `run` action is called.
 *
 * State transitions are only permited to be done by implementation within
 * corresponding AP power sequence driver context, and only one state transition
 * is allowed per driver thread loop iteration.
 *
 * Example of wrong use of `ap_pwrseq_sm_set_state`:
 *
 * @code{.c}
 * int arch_s0_run(void *data)
 * {
 *     // Transition started `entry` and `exit` functions called
 *     ap_pwrseq_sm_set_state(data, AP_POWER_STATE_S5);
 *     ...
 *     // Nothing happens `ap_pwrseq_sm_set_state` returns -EINVAL.
 *     ap_pwrseq_sm_set_state(data, AP_POWER_STATE_G3);
 * }
 * @endcode
 *
 * Example of correct use of `ap_pwrseq_sm_set_state`:
 *
 * @code{.c}
 * int arch_s0_run(void *data)
 * {
 *     if (...) {
 *         return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_S5);
 *     } else if (...) {
 *         return ap_pwrseq_sm_set_state(data, AP_POWER_STATE_G3);
 *     }
 *     return 0;
 * }
 * @endcod
 *
 * For this same reason, `ap_pwrseq_sm_set_state` should not be called within
 * `entry` or `exit` action handler.
 */

/*
 * This is required to ensure macro AP_POWER_SM_DEF_STATE_HANDLER handles
 * passing `NULL` properly.
 */
#ifdef NULL
#undef NULL
#define NULL 0
#else
#define NULL 0
#endif

/* User define action handler, each action handler must follow this type. */
typedef int (*ap_pwr_state_action_handler)(void *data);

#define AP_POWER_SM_HANDLER_DECL(action)           \
	void ap_pwrseq_sm_exec_##action##_handler( \
		void *const data, ap_pwr_state_action_handler handler)

AP_POWER_SM_HANDLER_DECL(entry);
AP_POWER_SM_HANDLER_DECL(run);
AP_POWER_SM_HANDLER_DECL(exit);

/**
 * @brief Macro to define action handler wrapper function.
 *
 * @param name Valid enumaration value of state.
 *
 * @param level One of the three AP power sequence levels: arch, chipset or app.
 *
 * @param action One of the three SMF action handlers: entry, run or exit.
 *
 * @param handler Action handler function of type `ap_pwr_state_action_handler`.
 *
 * @retval Defines static wrapper function of handler to be called by AP power
 * sequence state machine.
 **/
#define AP_POWER_SM_DEF_STATE_HANDLER(name, level, action, handler)            \
	static void ap_pwr_##name##_##level##_##action##_##handler(void *data) \
	{                                                                      \
		ap_pwrseq_sm_exec_##action##_handler(data, handler);           \
	}

/**
 * @brief Macro to define action handler wrapper function for a single level.
 *
 * @param name Valid enumaration value of state.
 *
 * @param level One of the three AP power sequence levels: arch, chipset or app.
 *
 * @param _entry Function called when entering into this state.
 *
 * @param _run Action handler function called when run operation is invoked.
 *
 * @param _exit Function called when exiting this state.
 *
 * @param handler Action handler function of type `ap_pwr_state_action_handler`.
 *
 * @retval Defines static wrapper function of handler to be called by AP power
 * sequence state machine.
 **/
#define AP_POWER_SM_DEF_STATE_HANDLERS(name, level, _entry, _run, _exit) \
	AP_POWER_SM_DEF_STATE_HANDLER(name, level, entry, _entry)        \
	AP_POWER_SM_DEF_STATE_HANDLER(name, level, run, _run)            \
	AP_POWER_SM_DEF_STATE_HANDLER(name, level, exit, _exit)

/**
 * @brief Macro to assemble action handler wrapper function name.
 *
 * @param name Valid enumaration value of state.
 *
 * @param level One of the three AP power sequence levels: arch, chipset or app.
 *
 * @param action One of the three SMF action handlers: entry, run or exit.
 *
 * @param handler Action handler function of type `ap_pwr_state_action_handler`.
 *
 * @retval Constructs static name of handler wrapper function to be called by
 * AP power sequence state machine.
 **/
#define AP_POWER_SM_ACTION(name, level, action, handler) \
	ap_pwr_##name##_##level##_##action##_##handler

/**
 * @brief Macro to create SMF state following AP power sequence.
 *
 * @param name Valid enumaration value of state.
 *
 * @param level One of the three AP power sequence levels: arch, chipset or app.
 *
 * @param _entry Function to be called when entrying state.
 *
 * @param _run Function to be called when executing `run` operation.
 *
 * @param _exit Function to be called when exiting state.
 *
 * @retval Defines global structure with action handlers to be used by AP
 * power sequence state machine.
 **/
#define AP_POWER_SM_CREATE_STATE(name, level, _entry, _run, _exit, parent)     \
	SMF_CREATE_STATE(AP_POWER_SM_ACTION(name, level, entry, _entry),       \
			 AP_POWER_SM_ACTION(name, level, run, _run),           \
			 AP_POWER_SM_ACTION(name, level, exit, _exit), parent, \
			 NULL)

/**
 * @brief Define architecture level state action handlers.
 *
 * @param name Valid enumaration value of state.
 *
 * @param entry Function to be called when entrying state.
 *
 * @param run Function to be called when executing `run` operation.
 *
 * @param exit Function to be called when exiting state.
 *
 * @retval Defines global structure with action handlers to be used by AP
 * power sequence state machine.
 **/
#define AP_POWER_ARCH_STATE_DEFINE(name, entry, run, exit)           \
	AP_POWER_SM_DEF_STATE_HANDLERS(name, arch, entry, run, exit) \
	const struct smf_state arch_##name##_actions =               \
		AP_POWER_SM_CREATE_STATE(name, arch, entry, run, exit, NULL)

/**
 * @brief Define chipset level state action handlers.
 *
 * @param name Valid enumaration value of state.
 *
 * @param entry Function to be called when entrying state.
 *
 * @param run Function to be called when executing `run` operation.
 *
 * @param exit Function to be called when exiting state.
 *
 * @retval Defines global structure with action handlers to be used by AP
 * power sequence state machine.
 **/
#define AP_POWER_CHIPSET_STATE_DEFINE(name, entry, run, exit)             \
	AP_POWER_SM_DEF_STATE_HANDLERS(name, chipset, entry, run, exit)   \
	const struct smf_state chipset_##name##_actions =                 \
		AP_POWER_SM_CREATE_STATE(name, chipset, entry, run, exit, \
					 &arch_##name##_actions)

/**
 * @brief Define application level state action handlers.
 *
 * @param name Valid enumaration value of state.
 *
 * @param entry Function to be called when entrying state.
 *
 * @param run Function to be called when executing `run` operation.
 *
 * @param exit Function to be called when exiting state.
 *
 * @retval Defines global structure with action handlers to be used by AP
 * power sequence state machine.
 **/
#define AP_POWER_APP_STATE_DEFINE(name, entry, run, exit)                     \
	AP_POWER_SM_DEF_STATE_HANDLERS(name, app, entry, run, exit)           \
	const struct ap_pwrseq_smf app_state_##name = {                       \
		.actions =                                                    \
			AP_POWER_SM_CREATE_STATE(name, app, entry, run, exit, \
						 &chipset_##name##_actions),  \
		.state = name                                                 \
	}

/**
 * @brief Define chipset level substate action handlers.
 *
 * @param name Valid enumaration value of state, as provided by devicetree
 * compatible with "ap-pwrseq-sub-states".
 *
 * @param entry Function to be called when entrying state.
 *
 * @param run Function to be called when executing `run` operation.
 *
 * @param exit Function to be called when exiting state.
 *
 * @param parent Valid enumaration value of parent state,
 *
 * @retval Defines global structure with action handlers to be used by AP
 * power sequence state machine.
 **/
#define AP_POWER_CHIPSET_SUB_STATE_DEFINE(name, entry, run, exit, parent)      \
	AP_POWER_SM_DEF_STATE_HANDLERS(name, chipset, entry, run, exit)        \
	const struct ap_pwrseq_smf chipset_##name##_actions = {                \
		.actions = AP_POWER_SM_CREATE_STATE(name, chipset, entry, run, \
						    exit,                      \
						    &arch_##parent##_actions), \
		.state = name                                                  \
	}

/**
 * @brief Define application level substate action handlers.
 *
 * @param name Valid enumaration value of state, as provided by devicetree
 * compatible with "ap-pwrseq-sub-states".
 *
 * @param entry Function to be called when entrying state.
 *
 * @param run Function to be called when executing `run` operation.
 *
 * @param exit Function to be called when exiting state.
 *
 * @param parent Valid enumaration value of parent state,
 *
 * @retval Defines global structure with action handlers to be used by AP
 * power sequence state machine.
 **/
#define AP_POWER_APP_SUB_STATE_DEFINE(name, entry, run, exit, parent)          \
	AP_POWER_SM_DEF_STATE_HANDLERS(name, app, entry, run, exit)            \
	const struct ap_pwrseq_smf app_state_##name = {                        \
		.actions =                                                     \
			AP_POWER_SM_CREATE_STATE(name, app, entry, run, exit,  \
						 &chipset_##parent##_actions), \
		.state = name                                                  \
	}

/**
 * @brief Sets AP power sequence state machine to provided state.
 *
 * This function is meant to be executed only within AP power sequence driver
 * thread context. `tid` was given in `ap_pwrseq_sm_init`.
 *
 * Only one state transition is permited within `run` iterations.
 *
 * @param data Pointer to AP power sequence state machine instance data.
 *
 * @param state Enum value of next state to be executed.
 *
 * @retval SUCCESS Upon success, current state `exit` action handler and next
 * state `entry` action handler will be executed.
 * @retval -EINVAL State provided is invalid.
 **/
int ap_pwrseq_sm_set_state(void *const data, enum ap_pwrseq_state state);

/**
 * @brief Check if events is set for current AP power sequence state machine
 * `run` iteration.
 *
 * @param data Pointer to AP power sequence state machine instance data.
 *
 * @param event Enum of test to be tested.
 *
 * @retval True If event is set, False otherwise.
 **/
bool ap_pwrseq_sm_is_event_set(void *const data, enum ap_pwrseq_event event);

/**
 * @brief Get state machine is entering.
 *
 * This function is meant to be executed only within AP power sequence driver
 * thread context. `tid` was given in `ap_pwrseq_sm_init`.
 *
 * @param data Pointer to AP power sequence state machine instance data.
 *
 * @retval Enum value Upon success.
 * @retval AP_POWER_STATE_UNDEF If state machine is not doing state transition.
 **/
enum ap_pwrseq_state ap_pwrseq_sm_get_entry_state(void *const data);

/**
 * @brief Get state machine is exiting.
 *
 * This function is meant to be executed only within AP power sequence driver
 * thread context. `tid` was given in `ap_pwrseq_sm_init`.
 *
 * @param data Pointer to AP power sequence state machine instance data.
 *
 * @retval Enum value Upon success.
 * @retval AP_POWER_STATE_UNDEF If state machine is not doing state transition.
 **/
enum ap_pwrseq_state ap_pwrseq_sm_get_exit_state(void *const data);
#endif /* _AP_PWRSEQ_SM_H_ */
