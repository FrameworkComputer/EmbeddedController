/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Type-C module */

#ifndef __CROS_EC_USB_TC_H
#define __CROS_EC_USB_TC_H

#include "usb_sm.h"
#include "usb_pd_tcpm.h"

#define TC_SET_FLAG(port, flag) atomic_or(&tc[port].flags, (flag))
#define TC_CLR_FLAG(port, flag) atomic_clear(&tc[port].flags, (flag))
#define TC_CHK_FLAG(port, flag) (tc[port].flags & (flag))

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
 * Returns true if TypeC State machine is in attached source state.
 *
 * @param port USB-C port number
 * @return 1 if in attached source state, else 0
 */
int tc_is_attached_src(int port);

/**
 * Returns true if TypeC State machine is in attached sink state.
 *
 * @param port USB-C port number
 * @return 1 if in attached source state, else 0
 */
int tc_is_attached_snk(int port);

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
 * Get current polarity
 *
 * @param port USB-C port number
 * @return 0 for CC1 as primary, 1 for CC2 as primary
 */
uint8_t tc_get_polarity(int port);

/**
 * Get Power Deliever communication state. If disabled, both protocol and policy
 * engine are disabled and should not run.
 *
 * @param port USB-C port number
 * @return 0 if pd is disabled, 1 is pd is enabled
 */
uint8_t tc_get_pd_enabled(int port);

/**
 * Set the power role
 *
 * @param port USB-C port number
 * @param role power role
 */
void tc_set_power_role(int port, int role);

/**
 * Set the data role
 *
 * @param port USB-C port number
 * @param role data role
 */
void tc_set_data_role(int port, int role);

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
 * Get loop timeout value
 *
 * @param port USB-C port number
 * @return time in ms
 */
uint64_t tc_get_timeout(int port);

/**
 * Policy Engine informs the Type-C state machine if the port partner
 * is dualrole power.
 *
 * @param port USB_C port number
 * @param en   1 if port partner is dualrole power, else 0
 */
void tc_partner_dr_power(int port, int en);

/**
 * Policy Engine informs the Type-C state machine if the port partner
 * has external power
 *
 * @param port USB_C port number
 * @param en   1 if port partner has external power, else 0
 */
void tc_partner_extpower(int port, int en);

/**
 * Policy Engine informs the Type-C state machine if the port partner
 * is USB comms.
 *
 * @param port USB_C port number
 * @param en   1 if port partner is USB comms, else 0
 */
void tc_partner_usb_comm(int port, int en);

/**
 * Policy Engine informs the Type-C state machine if the port partner
 * is dualrole data.
 *
 * @param port USB_C port number
 * @param en   1 if port partner is dualrole data, else 0
 */
void tc_partner_dr_data(int port, int en);

/**
 * Policy Engine informs the Type-C state machine if the port partner
 * had a previous pd connection
 *
 * @param port USB_C port number
 * @param en   1 if port partner had a previous pd connection, else 0
 */
void tc_pd_connection(int port, int en);

/**
 * Set loop timeout value
 *
 * @param port USB-C port number
 * @timeout time in ms
 */
void tc_set_timeout(int port, uint64_t timeout);

/**
 * Initiates a Power Role Swap from Attached.SRC to Attached.SNK. This function
 * has no effect if the current Type-C state is not Attached.SRC.
 *
 * @param port USB_C port number
 */
void tc_prs_src_snk_assert_rd(int port);

/**
 * Initiates a Power Role Swap from Attached.SNK to Attached.SRC. This function
 * has no effect if the current Type-C state is not Attached.SNK.
 *
 * @param port USB_C port number
 */
void tc_prs_snk_src_assert_rp(int port);

/**
 * Informs the Type-C State Machine that a Power Role Swap is complete.
 * This function is called from the Policy Engine.
 *
 * @param port USB_C port number
 */
void tc_pr_swap_complete(int port);

/**
 * Informs the Type-C State Machine that a Discover Identity is in progress.
 * This function is called from the Policy Engine.
 *
 * @param port USB_C port number
 */
void tc_disc_ident_in_progress(int port);

/**
 * Informs the Type-C State Machine that a Discover Identity is complete.
 * This function is called from the Policy Engine.
 *
 * @param port USB_C port number
 */
void tc_disc_ident_complete(int port);

/**
 * Instructs the Attached.SNK to stop drawing power. This function is called
 * from the Policy Engine and only has effect if the current Type-C state
 * Attached.SNK.
 *
 * @param port USB_C port number
 */
void tc_snk_power_off(int port);

/**
 * Instructs the Attached.SRC to stop supplying power. The function has
 * no effect if the current Type-C state is not Attached.SRC.
 *
 * @param port USB_C port number
 */
void tc_src_power_off(int port);

/**
 * Instructs the Attached.SRC to start supplying power. The function has
 * no effect if the current Type-C state is not Attached.SRC.
 *
 * @param port USB_C port number
 */
int tc_src_power_on(int port);

/**
 * Tests if a VCONN Swap is possible.
 *
 * @param port USB_C port number
 * @return 1 if vconn swap is possible, else 0
 */
int tc_check_vconn_swap(int port);

#ifdef CONFIG_USBC_VCONN
/**
 * Checks if VCONN is being sourced.
 *
 * @param port USB_C port number
 * @return 1 if vconn is being sourced, 0 if it's not, and -1 if
 *         can't answer at this time. -1 is returned if the current
 *         Type-C state is not Attached.SRC or Attached.SNK.
 */
int tc_is_vconn_src(int port);

/**
 * Instructs the Attached.SRC or Attached.SNK to start sourcing VCONN.
 * This function is called from the Policy Engine and only has effect
 * if the current Type-C state Attached.SRC or Attached.SNK.
 *
 * @param port USB_C port number
 */
void pd_request_vconn_swap_on(int port);

/**
 * Instructs the Attached.SRC or Attached.SNK to stop sourcing VCONN.
 * This function is called from the Policy Engine and only has effect
 * if the current Type-C state Attached.SRC or Attached.SNK.
 *
 * @param port USB_C port number
 */
void pd_request_vconn_swap_off(int port);
#endif


/**
 * Returns the polarity of a Sink.
 *
 * @param cc1 value of CC1 set by tcpm_get_cc
 * @param cc2 value of CC2 set by tcpm_get_cc
 * @return 0 if cc1 is connected, else 1 for cc2
 */
enum pd_cc_polarity_type get_snk_polarity(enum tcpc_cc_voltage_status cc1,
	enum tcpc_cc_voltage_status cc2);

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
 */
void tc_state_init(int port);

/**
 * Called by the state machine framework to handle events
 * that affect the state machine as a whole
 *
 * @param port USB-C port number
 * @param evt event
 */
void tc_event_check(int port, int evt);

/**
 * Runs the TypeC layer statemachine
 *
 * @param port USB-C port number
 */
void tc_run(const int port);

/**
 * Attempt to activate VCONN
 *
 * @param port USB-C port number
 */
void tc_vconn_on(int port);

/**
 * Start error recovery
 *
 * @param port USB-C port number
 */
void tc_start_error_recovery(int port);

/**
 * Hard Reset the TypeC port
 *
 * @param port USB-C port number
 */
void tc_hard_reset(int port);

#ifdef CONFIG_USB_TYPEC_CTVPD

/**
 * Resets the charge-through support timer. This can be
 * called many times but the support timer will only
 * reset once, while in the Attached.SNK state.
 *
 * @param port USB-C port number
 */
void tc_reset_support_timer(int port);

#else

/**
 *
 */
void tc_ctvpd_detected(int port);
#endif /* CONFIG_USB_TYPEC_CTVPD */
#endif /* __CROS_EC_USB_TC_H */

