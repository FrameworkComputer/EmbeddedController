/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB State Machine Framework */

#ifndef __CROS_EC_USB_SM_H
#define __CROS_EC_USB_SM_H

#define SM_OBJ(smo)    ((struct sm_obj *)&smo)
#define SUPER(r, sig, s)  ((((r) == 0) || ((sig) == ENTRY_SIG) || \
			((sig) == EXIT_SIG)) ? 0 : ((uintptr_t)(s)))
#define RUN_SUPER	1

/* Local state machine states */
enum sm_local_state {
	SM_INIT,
	SM_RUN,
	SM_PAUSED
};

/* State Machine signals */
enum signal {
	ENTRY_SIG = 0,
	RUN_SIG,
	EXIT_SIG,
	SUPER_SIG,
};

typedef unsigned int (*state_sig)(int port);
typedef unsigned int (*sm_state)(int port, enum signal sig);

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
void init_state(int port, struct sm_obj *obj, sm_state target);

/**
 * Changes a state machines state
 *
 * @param port   USB-C port number
 * @param obj    State machine object
 * @param target State to transition to
 * @return 0
 */
int set_state(int port, struct sm_obj *obj, sm_state target);

/**
 * Executes a state machine
 *
 * @param port USB-C port number
 * @param obj  State machine object
 * @param sig  State machine signal
 */
void exe_state(int port, struct sm_obj *obj, enum signal sig);

#endif /* __CROS_EC_USB_SM_H */
