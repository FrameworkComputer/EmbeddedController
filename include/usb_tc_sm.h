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
 * Set loop timeout value
 *
 * @param port USB-C port number
 * @timeout time in ms
 */
void tc_set_timeout(int port, uint64_t timeout);

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

