/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_USB_TC_DRP_ACC_TRYSRC_H
#define __CROS_EC_USB_TC_DRP_ACC_TRYSRC_H

#include "usb_sm.h"
#include "usb_tc_sm.h"

/* Port default state at startup */
#define TC_DEFAULT_STATE(port) TC_UNATTACHED_SNK

/**
 * This is the Type-C Port object that contains information needed to
 * implement a USB Type-C DRP with Accessory and Try.SRC module
 *   See Figure 4-16 in Release 1.4 of USB Type-C Spec.
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
	/* current port power role (SOURCE or SINK) */
	uint8_t power_role;
	/* current port data role (DFP or UFP) */
	uint8_t data_role;
	/* Port polarity : 0 => CC1 is CC line, 1 => CC2 is CC line */
	uint8_t polarity;
	/* port flags, see TC_FLAGS_* */
	uint32_t flags;
	/* event timeout */
	uint64_t evt_timeout;
	/* state machine event */
	int evt;
	/* Time a port shall wait before it can determine it is attached */
	uint64_t cc_debounce;
	/*
	 * Time a Sink port shall wait before it can determine it is detached
	 * due to the potential for USB PD signaling on CC as described in
	 * the state definitions.
	 */
	uint64_t pd_debounce;
#ifdef CONFIG_USB_PD_TRY_SRC
	/*
	 * Time a port shall wait before it can determine it is
	 * re-attached during the try-wait process.
	 */
	uint64_t try_wait_debounce;
#endif
	/* Voltage on CC pin */
	enum tcpc_cc_voltage_status cc_voltage;
	/* The cc state */
	enum pd_cc_states cc_state;
	/* Role toggle timer */
	uint64_t next_role_swap;
	/* Generic timer */
	uint64_t timeout;

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	/* Time to enter low power mode */
	uint64_t low_power_time;
	/* Tasks to notify after TCPC has been reset */
	int tasks_waiting_on_reset;
	/* Tasks preventing TCPC from entering low power mode */
	int tasks_preventing_lpm;
#endif
	/* Type-C current */
	typec_current_t typec_curr;
	/* Type-C current change */
	typec_current_t typec_curr_change;
	/* Attached ChromeOS device id, RW hash, and current RO / RW image */
	uint16_t dev_id;
	uint32_t dev_rw_hash[PD_RW_HASH_SIZE/4];
	enum ec_current_image current_image;
};

extern struct type_c tc[];

#endif /* __CROS_EC_USB_TC_DRP_ACC_TRYSRC_H */
