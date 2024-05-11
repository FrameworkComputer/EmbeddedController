/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Protocol Layer module */

#ifndef __CROS_EC_USB_PRL_H
#define __CROS_EC_USB_PRL_H
#include "common.h"
#include "timer.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usb_sm.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns TX success time stamp.
 *
 * @param port USB-C port number
 * @return the time stamp of TCPC tx success.
 **/
timestamp_t prl_get_tcpc_tx_success_ts(int port);

/**
 * Returns true if Protocol Layer State Machine is in run mode
 *
 * @param port USB-C port number
 * @return 1 if state machine is running, else 0
 */
int prl_is_running(int port);

/**
 * Returns true if the Protocol Layer State Machine is in the
 * process of transmitting or receiving chunked messages.
 *
 * @param port USB-C port number
 * @return true if sending or receiving a chunked message, else false
 */
bool prl_is_busy(int port);

/**
 * Sets the debug level for the PRL layer
 *
 * @param level debug level
 */
void prl_set_debug_level(enum debug_level level);

/**
 * Resets the Protocol Layer state machine but does not reset the stored PD
 * revisions of the partners.
 *
 * @param port USB-C port number
 */
void prl_reset_soft(int port);

/**
 * resets the stored pd revisions for each sop type to their default value, the
 * highest revision supported by this implementation. per pd r3.0 v2.0,
 * ss6.2.1.1.5, this should only happen upon detach, hard reset, or error
 * recovery.
 *
 * @param port USB-C port number
 */
void prl_set_default_pd_revision(int port);

/**
 * Runs the Protocol Layer State Machine
 *
 * @param port USB-C port number
 * @param evt  system event, ie: PD_EVENT_RX
 * @param en   0 to disable the machine, 1 to enable the machine
 */
void prl_run(int port, int evt, int en);

/**
 * Set the PD revision
 *
 * @param port USB-C port number
 * @param type port address
 * @param rev revision
 */
void prl_set_rev(int port, enum tcpci_msg_type type, enum pd_rev_type rev);

/**
 * Get the PD revision
 *
 * @param port USB-C port number
 * @param type port address
 * @return pd rev
 */
enum pd_rev_type prl_get_rev(int port, enum tcpci_msg_type type);

/**
 * Reset Tx and Rx message IDs for the specified partner to their initial
 * values.
 *
 * @param port USB-C port number
 * @param type Transmit type
 */
void prl_reset_msg_ids(int port, enum tcpci_msg_type type);

/**
 * Sends a PD control message
 *
 * @param port USB-C port number
 * @param type Transmit type
 * @param msg  Control message type
 */
void prl_send_ctrl_msg(int port, enum tcpci_msg_type type,
		       enum pd_ctrl_msg_type msg);

/**
 * Sends a PD data message
 *
 * @param port USB-C port number
 * @param type Transmit type
 * @param msg  Data message type
 */
void prl_send_data_msg(int port, enum tcpci_msg_type type,
		       enum pd_data_msg_type msg);

/**
 * Sends a PD extended data message
 *
 * @param port USB-C port number
 * @param type Transmit type
 * @param msg  Extended data message type
 */
void prl_send_ext_data_msg(int port, enum tcpci_msg_type type,
			   enum pd_ext_msg_type msg);

/**
 * Informs the Protocol Layer that a hard reset has completed
 *
 * @param port USB-C port number
 */
void prl_hard_reset_complete(int port);

/**
 * Policy Engine calls this function to execute a hard reset.
 *
 * @param port USB-C port number
 */
void prl_execute_hard_reset(int port);

/**
 * Enables or disables checking the data role on incoming messages.
 *
 * @param port USB-C port number
 * @param enable True to enable checking, false to disable checking
 */
void prl_set_data_role_check(int port, bool enable);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_USB_PRL_H */
