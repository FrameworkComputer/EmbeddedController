/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Policy Engine module */

#ifndef __CROS_EC_USB_PE_H
#define __CROS_EC_USB_PE_H

#include "usb_pd_tcpm.h"
#include "usb_sm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Policy Engine Receive and Transmit Errors */
enum pe_error {
	ERR_RCH_CHUNKED,
	ERR_RCH_MSG_REC,
	ERR_RCH_CHUNK_WAIT_TIMEOUT,
	ERR_TCH_CHUNKED,
	ERR_TCH_XMIT,
};

/**
 * Runs the Policy Engine State Machine
 *
 * @param port USB-C port number
 * @param evt  system event, ie: PD_EVENT_RX
 * @param en   0 to disable the machine, 1 to enable the machine
 */
void pe_run(int port, int evt, int en);

/**
 * Sets the debug level for the PRL layer
 *
 * @param level debug level
 */
void pe_set_debug_level(enum debug_level level);

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
 * @param type  port address where error was generated
 */
void pe_report_error(int port, enum pe_error e, enum tcpci_msg_type type);

/**
 * Informs the Policy Engine of a discard.
 *
 * @param port USB-C port number
 */
void pe_report_discard(int port);

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
 * Indicates if an explicit contract is in place
 *
 * @param port  USB-C port number
 * @return 1 if an explicit contract is in place, else 0
 */
int pe_is_explicit_contract(int port);

/*
 * Return true if port partner is dualrole capable
 *
 * @param port  USB-C port number
 */
int pd_is_port_partner_dualrole(int port);

/*
 * Informs the Policy Engine that it should invalidate the
 * explicit contract.
 *
 * @param port USB-C port number
 */
void pe_invalidate_explicit_contract(int port);

/*
 * Return true if the PE is in middle of a fast role swap (FRS). If so, the
 * Rp/Rd will be flipped from the actual power roles.
 *
 *
 * @param port USB-C port number
 */
bool pe_in_frs_mode(int port);

/*
 * Return true if the PE is is within an atomic
 * messaging sequence that it initiated with a SOP* port partner.
 *
 * Note the PRL layer polls this instead of using AMS_START and AMS_END
 * notification from the PE that is called out by the spec
 *
 * @param port USB-C port number
 */
bool pe_in_local_ams(int port);

/**
 * Returns the name of the current PE state
 *
 * @param port USB-C port number
 * @return name of current pe state
 */
const char *pe_get_current_state(int port);

/**
 * Returns the flag mask of the PE state machine
 *
 * @param port USB-C port number
 * @return flag mask of the pe state machine
 */
uint32_t pe_get_flags(int port);

/**
 * Sets the requested VCONN role for a forthcoming VCONN Swap DPM request.
 *
 * @param port USB-C port number
 * @param role Desired VCONN role after the swap
 */
void pe_set_requested_vconn_role(int port, enum pd_vconn_role role);

/**
 * Sets the Alert Data Object (ADO) in the PE state
 *
 * @param port USB-C port number
 * @param data - ADO data sent during alert messages
 * @return EC_SUCCESS if successful and EC_ERROR_BUSY if not
 */
int pe_set_ado(int port, uint32_t data);

/**
 * Clears the Alert Data Object (ADO) in the PE state
 *
 * @param port USB-C port number
 */
void pe_clear_ado(int port);

#ifdef TEST_BUILD
/**
 * Clears all internal port data, as we would on a detach event
 *
 * @param port USB-C port number
 */
void pe_clear_port_data(int port);
#endif /* TEST_BUILD */

/**
 * Check whether the port is in EPR mode or not.
 *
 * @param port USB-C port number
 * @return true if the port is in EPR mode or false.
 */
bool pe_snk_in_epr_mode(int port);

/**
 * Make a sink exit EPR mode explicitly.
 *
 * @param port USB-C port number
 */
void pe_snk_epr_explicit_exit(int port);

/**
 * Checks whether the port is ready for EPR entry.
 *
 * @param port USB-C port number
 * @return true if the port can enter EPR mode or false.
 */
bool pe_snk_can_enter_epr_mode(int port);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_USB_PE_H */
