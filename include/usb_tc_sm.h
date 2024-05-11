/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Type-C module */

#ifndef __CROS_EC_USB_TC_H
#define __CROS_EC_USB_TC_H

#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usb_sm.h"

#ifdef __cplusplus
extern "C" {
#endif

enum try_src_override_t {
	TRY_SRC_OVERRIDE_OFF,
	TRY_SRC_OVERRIDE_ON,
	TRY_SRC_NO_OVERRIDE
};

/*
 * Type C supply voltage (mV)
 *
 * This is the maximum voltage a sink can request
 * while charging.
 */
#define TYPE_C_VOLTAGE 5000 /* mV */

/*
 * Type C default sink current (mA)
 *
 * This is the maximum current a sink can draw if charging
 * while in the Audio Accessory State.
 */
#define TYPE_C_AUDIO_ACC_CURRENT 500 /* mA */

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
 * Get cable plug setting. This should be constant per build. This replaces
 * the power role bit in PD header for SOP' and SOP" packets.
 *
 * @param port USB-C port number
 * @return PD cable plug setting
 */
enum pd_cable_plug tc_get_cable_plug(int port);

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
void tc_set_power_role(int port, enum pd_power_role role);

/**
 * Set the data role
 *
 * @param port USB-C port number
 * @param role data role
 */
void tc_set_data_role(int port, enum pd_data_role role);

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
 * Policy Engine informs the Type-C state machine if the port partner
 * is dualrole power.
 *
 * @param port USB_C port number
 * @param en   1 if port partner is dualrole power, else 0
 */
void tc_partner_dr_power(int port, int en);

/**
 * Policy Engine informs the Type-C state machine if the port partner
 * has unconstrained power
 *
 * @param port USB_C port number
 * @param en   1 if port partner has unconstrained power, else 0
 */
void tc_partner_unconstrainedpower(int port, int en);

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
 * Informs the Type-C State Machine that a Power Role Swap is starting.
 * This function is called from the Policy Engine.
 *
 * @parm port USB_C port number
 */
void tc_request_power_swap(int port);

/**
 * Informs the Type-C State Machine that a Power Role Swap is complete.
 * This function is called from the Policy Engine.
 *
 * @param port USB_C port number
 * @param success swap completed normally
 */
void tc_pr_swap_complete(int port, bool success);

/**
 * The Type-C state machine updates the SLEEP_MASK_USB_PD mask for the
 * case that TCPC wants to set/clear SLEEP_MASK_USB_PD mask only by
 * itself, ex. TCPC embedded in EC.
 *
 * @param port USB_C port number
 */
__override_proto void tc_update_pd_sleep_mask(int port);

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

/**
 * Checks if VCONN is being sourced.
 *
 * @param port USB_C port number
 * @return 1 if vconn is being sourced, 0 if it's not.
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

/**
 * Returns the polarity of a Sink.
 *
 * @param cc1 value of CC1 set by tcpm_get_cc
 * @param cc2 value of CC2 set by tcpm_get_cc
 * @return 0 if cc1 is connected, else 1 for cc2
 */
enum tcpc_cc_polarity get_snk_polarity(enum tcpc_cc_voltage_status cc1,
				       enum tcpc_cc_voltage_status cc2);

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
 * Sets the debug level for the TC layer
 *
 * @param level debug level
 */
void tc_set_debug_level(enum debug_level level);

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
void tc_hard_reset_request(int port);

/**
 * Hard Reset is complete for the TypeC port
 *
 * @param port USB-C port number
 */
void tc_hard_reset_complete(int port);

/**
 * Start the state machine event loop
 *
 * @param port USB-C port number
 */
void tc_start_event_loop(int port);

/**
 * Pauses the state machine event loop
 *
 * @param port USB-C port number
 */
void tc_pause_event_loop(int port);

/**
 * Determine if the state machine event loop is paused
 *
 * @param port USB-C port number
 * @return true if paused, else false
 */
bool tc_event_loop_is_paused(int port);

/**
 * Allow system to override the control of TrySrc
 *
 * @param en	TRY_SRC_OVERRIDE_OFF - Force TrySrc OFF
 *		TRY_SRC_OVERRIDE_ON - Force TrySrc ON
 *		TRY_SRC_NO_OVERRIDE - Allow state machine to control TrySrc
 */
void tc_try_src_override(enum try_src_override_t ov);

/**
 * Get state of try_src_override
 *
 * @return	TRY_SRC_OVERRIDE_OFF - TrySrc is forced OFF
 *		TRY_SRC_OVERRIDE_ON - TrySrc is forced ON
 *		TRY_SRC_NO_OVERRIDE - TypeC state machine controls TrySrc
 */
enum try_src_override_t tc_get_try_src_override(void);

/**
 * Returns the name of the current typeC state
 *
 * @param port USB-C port number
 * @return name of current typeC state
 */
const char *tc_get_current_state(int port);

/**
 * Returns the flag mask of the typeC state machine
 *
 * @param port USB-C port number
 * @return flag mask of the typeC state machine
 */
uint32_t tc_get_flags(int port);

/**
 * USB retimer firmware update set run flag
 * Setting this flag indicates firmware update operations can be
 * processed unconditionally.
 *
 * @param port USB-C port number
 */
void tc_usb_firmware_fw_update_run(int port);

/**
 * USB retimer firmware update set limited run flag
 * Setting this flag indicates firmware update operations can be
 * processed under limitation: PD task has to be suspended.
 *
 * @param port USB-C port number
 */
void tc_usb_firmware_fw_update_limited_run(int port);

#ifdef CONFIG_USB_CTVPD

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
#endif /* CONFIG_USB_CTVPD */

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_USB_TC_H */
