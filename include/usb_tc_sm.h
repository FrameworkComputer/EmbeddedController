/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Type-C module */

#ifndef __CROS_EC_USB_TC_H
#define __CROS_EC_USB_TC_H

#include "usb_sm.h"

enum typec_state_id {
	TC_DISABLED,
	TC_UNATTACHED_SNK,
	TC_ATTACH_WAIT_SNK,
	TC_ATTACHED_SNK,
#if !defined(CONFIG_USB_TYPEC_VPD)
	TC_ERROR_RECOVERY,
	TC_UNATTACHED_SRC,
	TC_ATTACH_WAIT_SRC,
	TC_ATTACHED_SRC,
#endif
#if !defined(CONFIG_USB_TYPEC_CTVPD) && !defined(CONFIG_USB_TYPEC_VPD)
	TC_AUDIO_ACCESSORY,
	TC_ORIENTED_DEBUG_ACCESSORY_SRC,
	TC_UNORIENTED_DEBUG_ACCESSORY_SRC,
	TC_DEBUG_ACCESSORY_SNK,
	TC_TRY_SRC,
	TC_TRY_WAIT_SNK,
	TC_CTUNATTACHED_SNK,
	TC_CTATTACHED_SNK,
#endif
#if defined(CONFIG_USB_TYPEC_CTVPD)
	TC_CTTRY_SNK,
	TC_CTATTACHED_UNSUPPORTED,
	TC_CTATTACH_WAIT_UNSUPPORTED,
	TC_CTUNATTACHED_UNSUPPORTED,
	TC_CTUNATTACHED_VPD,
	TC_CTATTACH_WAIT_VPD,
	TC_CTATTACHED_VPD,
	TC_CTDISABLED_VPD,
	TC_TRY_SNK,
	TC_TRY_WAIT_SRC,
#endif
	/* Number of states. Not an actual state. */
	TC_STATE_COUNT,
};

extern const char * const tc_state_names[];

#define TC_SET_FLAG(port, flag) atomic_or(&tc[port].flags, (flag))
#define TC_CLR_FLAG(port, flag) atomic_clear(&tc[port].flags, (flag))
#define TC_CHK_FLAG(port, flag) (tc[port].flags & (flag))

/*
 * TC_OBJ is a convenience macro to access struct sm_obj, which
 * must be the first member of struct type_c.
 */
#define TC_OBJ(port)   (SM_OBJ(tc[port]))

/*
 * Type C supply voltage (mV)
 *
 * This is the maximum voltage a sink can request
 * while charging.
 */
#define TYPE_C_VOLTAGE  5000 /* mV */

/*
 * Type C default sink current (mA)
 *
 * This is the maximum current a sink can draw if charging
 * while in the Audio Accessory State.
 */
#define TYPE_C_AUDIO_ACC_CURRENT  500 /* mA */

/**
 * Get the id of the current Type-C state
 *
 * @param port USB-C port number
 */
enum typec_state_id get_typec_state_id(int port);

/**
 * Get current data role
 *
 * @param port USB-C port number
 * @return 0 for ufp, 1 for dfp, 2 for disconnected
 */
int tc_get_data_role(int port);

/**
 * Get current power role
 *
 * @param port USB-C port number
 * @return 0 for sink, 1 for source or vpd
 */
int tc_get_power_role(int port);

/**
 * Set the power role
 *
 * @param port USB-C port number
 * @param role power role
 */
void tc_set_power_role(int port, int role);

/**
 * Sets the USB Mux depending on current data role
 *   Mux is connected except when:
 *     1) PD is disconnected
 *     2) Current data role is UFP and we only support DFP
 *
 *  @param port USB-C port number
 */
void set_usb_mux_with_current_data_role(int port);

/**
 * Set loop timeout value
 *
 * @param port USB-C port number
 * @timeout time in ms
 */
void tc_set_timeout(int port, uint64_t timeout);

/**
 * Returns the polarity of a Sink.
 *
 * @param cc1 value of CC1 set by tcpm_get_cc
 * @param cc2 value of CC2 set by tcpm_get_cc
 * @return 0 if cc1 is connected, else 1 for cc2
 */
enum pd_cc_polarity_type get_snk_polarity(int cc1, int cc2);

/**
 * Restarts the TCPC
 *
 * @param port USB-C port number
 * @returns EC_SUCCESS on success
 */
int tc_restart_tcpc(int port);

/**
 * Sets the polarity of the port
 *
 * @param port USB-C port number
 * @param polarity 0 for CC1, else 1 for CC2
 */
void set_polarity(int port, int polarity);

/**
 * Called by the state machine framework to initialize the
 * TypeC state machine
 *
 * @param port USB-C port number
 * @param sm_state initial state of the state machine. Must be
 *			UNATTACHED_SNK or UNATTACHED_SRC, any other
 *			state id value will be interpreted as
 *			UNATTACHED_SNK.
 */
void tc_state_init(int port, enum typec_state_id start_state);

/**
 * Called by the state machine framework to handle events
 * that affect the state machine as a whole
 *
 * @param port USB-C port number
 * @param evt event
 */
void tc_event_check(int port, int evt);

#ifdef CONFIG_USB_TYPEC_CTVPD
/**
 * Resets the charge-through support timer. This can be
 * called many times but the support timer will only
 * reset once, while in the Attached.SNK state.
 *
 * @param port USB-C port number
 */
void tc_reset_support_timer(int port);

#endif /* CONFIG_USB_TYPEC_CTVPD */
#endif /* __CROS_EC_USB_TC_H */

