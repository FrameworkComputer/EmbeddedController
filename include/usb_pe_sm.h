/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Policy Engine module */

#ifndef __CROS_EC_USB_PE_H
#define __CROS_EC_USB_PE_H

#include "usb_sm.h"

/* Policy Engine Receive and Transmit Errors */
enum pe_error {
	ERR_RCH_CHUNKED,
	ERR_RCH_MSG_REC,
	ERR_TCH_CHUNKED,
	ERR_TCH_XMIT,
};

/*
 * Device Policy Manager Requests.
 * NOTE: These are usually set by host commands from the AP.
 */
enum pe_dpm_request {
	DPM_REQUEST_DR_SWAP             = BIT(0),
	DPM_REQUEST_PR_SWAP             = BIT(1),
	DPM_REQUEST_VCONN_SWAP          = BIT(2),
	DPM_REQUEST_GOTO_MIN            = BIT(3),
	DPM_REQUEST_SRC_CAP_CHANGE      = BIT(4),
	DPM_REQUEST_GET_SNK_CAPS        = BIT(5),
	DPM_REQUEST_SEND_PING           = BIT(6),
	DPM_REQUEST_SOURCE_CAP          = BIT(7),
	DPM_REQUEST_NEW_POWER_LEVEL     = BIT(8),
	DPM_REQUEST_DISCOVER_IDENTITY   = BIT(9),
	DPM_REQUEST_EXIT_DP_MODE        = BIT(10),
	DPM_REQUEST_SVDM                = BIT(11),
};

/**
 * Initialize the Policy Engine State Machine
 *
 * @param port USB-C port number
 */
void pe_init(int port);

/**
 * Runs the Policy Engine State Machine
 *
 * @param port USB-C port number
 * @param evt  system event, ie: PD_EVENT_RX
 * @param en   0 to disable the machine, 1 to enable the machine
 */
void pe_run(int port, int evt, int en);

/**
 * Informs the Policy Engine that a message was successfully sent
 *
 * @param port USB-C port number
 */
void pe_message_sent(int port);

/**
 * Informs the Policy Engine of an error.
 *
 * @param port USB-C port number
 * @param  e    error
 */
void pe_report_error(int port, enum pe_error e);

/**
 * Called by the Protocol Layer to informs the Policy Engine
 * that a message has been received.
 *
 * @param port USB-C port number
 */
void pe_message_received(int port);

/**
 * Informs the Policy Engine that a hard reset was received.
 *
 * @param port USB-C port number
 */
void pe_got_hard_reset(int port);

/**
 * Informs the Policy Engine that a soft reset was received.
 *
 * @param port USB-C port number
 */
void pe_got_soft_reset(int port);

/**
 * Informs the Policy Engine that a hard reset was sent.
 *
 * @param port USB-C port number
 */
void pe_hard_reset_sent(int port);

/**
 * Exit DP mode
 *
 * @param port USB-C port number
 */
void pe_exit_dp_mode(int port);

/**
 * Get the id of the current Policy Engine state
 *
 * @param port USB-C port number
 */
enum pe_states pe_get_state_id(int port);

/**
 * Indicates if the Policy Engine State Machine is running.
 *
 * @param port USB-C port number
 * @return 1 if policy engine state machine is running, else 0
 */
int pe_is_running(int port);

/**
 * Informs the Policy Engine that the Power Supply is at it's default state
 *
 * @param port USB-C port number
 */
void pe_ps_reset_complete(int port);

/**
 * Informs the Policy Engine that a VCONN Swap has completed
 *
 * @param port USB-C port number
 */
void pe_vconn_swap_complete(int port);

/**
 * Instructs the Policy Engine to send a Vendor Defined Message
 *
 * @param port  USB-C port number
 * @param vid   Vendor ID
 * @param cmd   Vendor Defined Command
 * @param data  Vendor Defined Data
 * @param count Size of Vendor Defined Data in 32-bit objects
 */
void pe_send_vdm(int port, uint32_t vid, int cmd, const uint32_t *data,
				int count);

/**
 * Indicates if an explicit contract is in place
 *
 * @param port  USB-C port number
 * @return 1 if an explicit contract is in place, else 0
 */
int pe_is_explicit_contract(int port);

/**
 * Instruct the Policy Engine to perform a Device Policy Manager Request
 * This function is called from the Device Policy Manager and only has effect
 * if the current Policy Engine state is Src.Ready or Snk.Ready.
 *
 * @param port  USB-C port number
 * @param req   Device Policy Manager Request
 */
void pe_dpm_request(int port, enum pe_dpm_request req);

#endif /* __CROS_EC_USB_PE_H */

