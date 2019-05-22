/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_USB_TC_VPD_SM_H
#define __CROS_EC_USB_TC_VPD_SM_H

#include "usb_sm.h"
#include "usb_tc_sm.h"

/**
 * This is the Type-C Port object that contains information needed to
 * implement a VCONN Powered Device.
 */
struct type_c {
	/*
	 * struct sm_obj must be first. This is the state machine
	 * object that keeps track of the current and last state
	 * of the state machine.
	 */
	struct sm_obj obj;
	/* state id */
	enum typec_state_id state_id;
	/* current port power role (VPD, SOURCE or SINK) */
	uint8_t power_role;
	/* current port data role (DFP or UFP) */
	uint8_t data_role;
	/* bool: enable power delivery state machines */
	uint8_t pd_enable;
	/* event timeout */
	uint64_t evt_timeout;
	/* state machine event */
	int evt;
	/* port flags, see TC_FLAGS_* */
	uint32_t flags;
	/* Time a port shall wait before it can determine it is attached */
	uint64_t cc_debounce;
	/* VPD host port cc state */
	enum pd_cc_states host_cc_state;
	uint8_t ct_cc;
};

extern struct type_c tc[];

#endif /* __CROS_EC_USB_TC_VPD_SM_H */
