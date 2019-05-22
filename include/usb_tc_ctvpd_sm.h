/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_USB_TC_CTVPD_SM_H
#define __CROS_EC_USB_TC_CTVPD_SM_H

#include "usb_sm.h"
#include "usb_tc_sm.h"

/**
 * This is the Type-C Port object that contains information needed to
 * implement a Charge Through VCONN Powered Device.
 */
struct type_c {
	/* struct sm_obj must be first */
	struct sm_obj obj;
	/* state id */
	enum typec_state_id state_id;
	/* current port power role (VPD, SOURCE or SINK) */
	uint8_t power_role;
	/* current port data role (DFP or UFP) */
	uint8_t data_role;
	/* enable power delivery state machines */
	uint8_t pd_enable;
	/* event timeout */
	uint64_t evt_timeout;
	/* state machine event */
	int evt;
	/* port flags, see TC_FLAGS_* */
	uint32_t flags;
	/*
	 * Time a charge-through port shall wait before it can determine it
	 * is attached
	 */
	uint64_t cc_debounce;
	/* Time a host port shall wait before it can determine it is attached */
	uint64_t host_cc_debounce;
	/* Time a Sink port shall wait before it can determine it is detached
	 * due to the potential for USB PD signaling on CC as described in
	 * the state definitions.
	 */
	uint64_t pd_debounce;
	/* Maintains state of billboard device */
	int billboard_presented;
	/*
	 * Time a port shall wait before it can determine it is
	 * re-attached during the try-wait process.
	 */
	uint64_t try_wait_debounce;
	/* charge-through support timer */
	uint64_t support_timer;
	/* reset the charge-through support timer */
	uint8_t support_timer_reset;
	/* VPD host port cc state */
	enum pd_cc_states host_cc_state;
	uint8_t ct_cc;
	/* The cc state */
	enum pd_cc_states cc_state;
	uint64_t next_role_swap;
};

extern struct type_c tc[];

#endif /* __CROS_EC_USB_TC_CTVPD_SM_H */
