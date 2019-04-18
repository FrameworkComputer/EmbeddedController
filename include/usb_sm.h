/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB State Machine Framework */

#ifndef __CROS_EC_USB_SM_H
#define __CROS_EC_USB_SM_H

#define DECLARE_SM_FUNC_(prefix, name, run, exit) \
			DECLARE_SM_FUNC_##run(prefix, name); \
			DECLARE_SM_FUNC_##exit(prefix, name)

#define DECLARE_SM_FUNC_WITH_RUN(prefix, name)    static int \
				prefix##_##name##_run(int port)

#define DECLARE_SM_FUNC_WITH_EXIT(prefix, name)   static int \
				prefix##_##name##_exit(int port)

#define DECLARE_SM_FUNC_NOOP(prefix, name)

#define DECLARE_SM_SIG_RUN(prefix, name, run)  DECLARE_SM_##run(prefix, name)
#define DECLARE_SM_WITH_RUN(prefix, name)   prefix##_##name##_run

#define DECLARE_SM_SIG_EXIT(prefix, name, exit)  DECLARE_SM_##exit(prefix, name)
#define DECLARE_SM_WITH_EXIT(prefix, name)   prefix##_##name##_exit

#define DECLARE_SM_NOOP(prefix, name)  sm_do_nothing

/*
 * Helper macro for the declaration of states.
 *
 * @param prefix - prefix of state function name
 * @param name   - name of state
 * @param run    - if WITH_RUN, generates a run state function name
 *                 if NOOP, generates a do nothing function name
 * @param exit   - if WITH_EXIT, generates an exit state function name
 *                 if NOOP, generates a do nothing function name
 *
 * EXAMPLE:
 *
 * DECLARE_STATE(tc, test, WITH_RUN, WITH_EXIT); generates the following:
 *
 * static unsigned int tc_test(int port, enum sm_signal sig);
 * static unsigned int tc_test_entry(int port);
 * static unsigned int tc_test_run(int port);
 * static unsigned int tc_test_exit(int port);
 * static const state_sig tc_test_sig[] = {
 *	tc_test_entry,
 *	tc_test_run,
 *	tc_test_exit,
 *	sm_get_super_state };
 *
 * DECLARE_STATE(tc, test, NOOP, NOOP); generates the following:
 *
 * static unsigned int tc_test(int port, enum sm_signal sig);
 * static unsigned int tc_test_entry(int port);
 * static const state_sig tc_test_sig[] = {
 *      tc_test_entry,
 *      sm_do_nothing,
 *      sm_do_nothing,
 *      sm_get_super_state };
 */
#define DECLARE_STATE(prefix, name, run, exit) \
static int prefix##_##name(int port, enum sm_signal sig); \
static int prefix##_##name##_entry(int port); \
DECLARE_SM_FUNC_(prefix, name, run, exit); \
static const state_sig prefix##_##name##_sig[] = { \
prefix##_##name##_entry, \
DECLARE_SM_SIG_RUN(prefix, name, run), \
DECLARE_SM_SIG_EXIT(prefix, name, exit), \
sm_get_super_state \
}

#define SM_OBJ(smo)    ((struct sm_obj *)&smo)
#define SM_SUPER(r, sig, s)  ((((r) == 0) || ((sig) == SM_ENTRY_SIG) || \
			((sig) == SM_EXIT_SIG)) ? 0 : ((uintptr_t)(s)))
#define SM_RUN_SUPER	1

/* Local state machine states */
enum sm_local_state {
	SM_INIT,
	SM_RUN,
	SM_PAUSED
};

/* State Machine signals */
enum sm_signal {
	SM_ENTRY_SIG = 0,
	SM_RUN_SIG,
	SM_EXIT_SIG,
	SM_SUPER_SIG,
};

typedef int (*state_sig)(int port);
typedef int (*sm_state)(int port, enum sm_signal sig);

struct sm_obj {
	sm_state task_state;
	sm_state last_state;
};

/**
 * Initialize a State Machine
 *
 * @param port   USB-C port number
 * @param obj    State machine object
 * @param target Initial state of state machine
 */
void sm_init_state(int port, struct sm_obj *obj, sm_state target);

/**
 * Changes a state machines state
 *
 * @param port   USB-C port number
 * @param obj    State machine object
 * @param target State to transition to
 * @return 0
 */
int sm_set_state(int port, struct sm_obj *obj, sm_state target);

/**
 * Runs a state machine
 *
 * @param port USB-C port number
 * @param obj  State machine object
 * @param sig  State machine signal
 */
void sm_run_state_machine(int port, struct sm_obj *obj, enum sm_signal sig);

/**
 * Substitute this function for states that do not implement a
 * run or exit state.
 *
 * @param port USB-C port number
 * @return 0
 */
int sm_do_nothing(int port);

/**
 * Called by the state machine framework to execute a states super state.
 *
 * @param port USB-C port number
 * @return RUN_SUPER
 */
int sm_get_super_state(int port);

#endif /* __CROS_EC_USB_SM_H */
