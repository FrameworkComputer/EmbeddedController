/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB State Machine Framework */

#ifndef __CROS_EC_USB_SM_H
#define __CROS_EC_USB_SM_H

#include "compiler.h" /* for typeof() on Zephyr */

#ifdef __cplusplus
extern "C" {
#endif

/* Function pointer that implements a portion of a usb state */
typedef void (*state_execution)(const int port);

/*
 * General usb state that can be used in multiple state machines.
 *
 * entry - Optional method that will be run when this state is entered
 * run   - Optional method that will be run repeatedly during state machine loop
 * exit  - Optional method that will be run when this state exists
 * parent- Optional parent usb_state that contains common entry/run/exit
 *	implementation among various child usb_states.
 *	entry: Parent function executes BEFORE child function.
 *	run: Parent function executes AFTER child function.
 *	exit: Parent function executes AFTER child function.
 *
 *	Note: When transitioning between two child states with a shared parent,
 *	that parent's exit and entry functions do not execute.
 */
struct usb_state {
	const state_execution entry;
	const state_execution run;
	const state_execution exit;
	const struct usb_state *parent;
};

typedef const struct usb_state *usb_state_ptr;

/* Defines the current context of the usb statemachine. */
struct sm_ctx {
	usb_state_ptr current;
	usb_state_ptr previous;
	/* We use intptr_t type to accommodate host tests ptr size variance */
	intptr_t internal[2];
};

/* Local state machine states */
enum sm_local_state {
	SM_INIT = 0, /* Ensure static variables initialize to SM_INIT */
	SM_RUN,
	SM_PAUSED,
};

/*
 * A state machine can use these debug levels to regulate the amount of debug
 * information printed on the EC console
 *
 * The states currently defined are
 *   Level 0: disabled
 *   Level 1: state names
 *
 * Note that higher log level causes timing changes and thus may affect
 * performance.
 */
enum debug_level {
	DEBUG_DISABLE,
	DEBUG_LEVEL_1,
	DEBUG_LEVEL_2,
	DEBUG_LEVEL_3,
	DEBUG_LEVEL_MAX = DEBUG_LEVEL_3
};

/**
 * Changes a state machines state. This handles exiting the previous state and
 * entering the target state. A common parent state will not exited nor be
 * re-entered.
 *
 * @param port      USB-C port number
 * @param ctx       State machine context
 * @param new_state State to transition to (NULL is valid and exits all states)
 */
void set_state(int port, struct sm_ctx *ctx, usb_state_ptr new_state);

/**
 * Runs one iteration of a state machine (including any parent states)
 *
 * @param port USB-C port number
 * @param ctx  State machine context
 */
void run_state(int port, struct sm_ctx *ctx);

#ifdef TEST_BUILD
/*
 * Struct for test builds that allow unit tests to easily iterate through
 * state machines
 */
struct test_sm_data {
	/* Base pointer of the state machine array */
	const usb_state_ptr base;
	/* Size fo the state machine array above */
	const int size;
	/* The array of names for states, can be NULL */
	const char *const *const names;
	/* The size of the above names array */
	const int names_size;
};
#endif

/* Creates a state machine state that will never link. Useful with IS_ENABLED */
#define GEN_NOT_SUPPORTED(state) extern typeof(state) state##_NOT_SUPPORTED

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_USB_SM_H */
