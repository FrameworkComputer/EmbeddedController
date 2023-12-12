/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_USB_PE_PRIVATE_H
#define __CROS_EC_USB_PE_PRIVATE_H

/** Internal header file for usb_pe.
 *
 * EC code should not normally include this. These are exposed so they can be
 * used by unit test code.
 */

enum {
	/* At least one successful PD communication packet received from port
	   partner */
	PE_FLAGS_PD_CONNECTION_FN = 0,
	/* Accept message received from port partner */
	PE_FLAGS_ACCEPT_FN,
	/* Power Supply Ready message received from port partner */
	PE_FLAGS_PS_READY_FN,
	/* Protocol Error was determined based on error recovery current state
	 */
	PE_FLAGS_PROTOCOL_ERROR_FN,
	/* Set if we are in Modal Operation */
	PE_FLAGS_MODAL_OPERATION_FN,
	/* A message we requested to be sent has been transmitted */
	PE_FLAGS_TX_COMPLETE_FN,
	/* A message sent by a port partner has been received */
	PE_FLAGS_MSG_RECEIVED_FN,
	/* A hard reset has been requested but has not been sent, not currently
	   used */
	PE_FLAGS_HARD_RESET_PENDING_FN,
	/* Port partner sent a Wait message. Wait before we resend our message
	 */
	PE_FLAGS_WAIT_FN,
	/* An explicit contract is in place with our port partner */
	PE_FLAGS_EXPLICIT_CONTRACT_FN,
	/* Waiting for Sink Capabailities timed out.  Used for retry error
	   handling */
	PE_FLAGS_SNK_WAIT_CAP_TIMEOUT_FN,
	/* Power Supply voltage/current transition timed out */
	PE_FLAGS_PS_TRANSITION_TIMEOUT_FN,
	/* Flag to note current Atomic Message Sequence is interruptible */
	PE_FLAGS_INTERRUPTIBLE_AMS_FN,
	/* Flag to note Power Supply reset has completed */
	PE_FLAGS_PS_RESET_COMPLETE_FN,
	/* VCONN swap operation has completed */
	PE_FLAGS_VCONN_SWAP_COMPLETE_FN,
	/* Flag to note PR Swap just completed for Startup entry */
	PE_FLAGS_PR_SWAP_COMPLETE_FN,
	/* Flag to note FRS/PRS context in shared state machine path */
	PE_FLAGS_FAST_ROLE_SWAP_PATH_FN,
	/* Flag to note if FRS listening is enabled */
	PE_FLAGS_FAST_ROLE_SWAP_ENABLED_FN,
	/* Flag to note TCPC passed on FRS signal from port partner */
	PE_FLAGS_FAST_ROLE_SWAP_SIGNALED_FN,
	/* TODO: POLICY decision: Triggers a DR SWAP attempt from UFP to DFP */
	PE_FLAGS_DR_SWAP_TO_DFP_FN,
	/*
	 * TODO: POLICY decision
	 * Flag to trigger a message resend after receiving a WAIT from port
	 * partner
	 */
	PE_FLAGS_WAITING_PR_SWAP_FN,
	/* FLAG is set when an AMS is initiated locally. ie. AP requested a
	   PR_SWAP */
	PE_FLAGS_LOCALLY_INITIATED_AMS_FN,
	/* Flag to note the first message sent in PE_SRC_READY and PE_SNK_READY
	 */
	PE_FLAGS_FIRST_MSG_FN,
	/* Flag to continue a VDM request if it was interrupted */
	PE_FLAGS_VDM_REQUEST_CONTINUE_FN,
	/* FLAG to track that VDM request to port partner timed out */
	PE_FLAGS_VDM_REQUEST_TIMEOUT_FN,
	/* FLAG to note message was discarded due to incoming message */
	PE_FLAGS_MSG_DISCARDED_FN,
	/* FLAG to note that hard reset can't be performed due to battery low */
	PE_FLAGS_SNK_WAITING_BATT_FN,
	/* FLAG to note that a data reset is complete */
	PE_FLAGS_DATA_RESET_COMPLETE_FN,
	/* Waiting for SRC to SNK settle time */
	PE_FLAGS_SRC_SNK_SETTLE_FN,
	/*
	 * Indicates the port is in the process of entering EPR mode. For a
	 * sink, the SM is in and after PE_SNK_EPR_Mode_Entry. For a source,
	 * the SM is in and after PE_SRC_EPR_Mode_Entery_ACK.
	 */
	PE_FLAGS_ENTERING_EPR_FN,
	/* In EPR mode */
	PE_FLAGS_IN_EPR_FN,
	/*
	 * Indicate the port exited EPR mode explicitly. If this is set, the
	 * port wouldn't enter EPR mode automatically.
	 */
	PE_FLAGS_EPR_EXPLICIT_EXIT_FN,
	/* Discovery disabled due to UFP/PD 2.0 constraint. */
	PE_FLAGS_DISCOVERY_DISABLED_FN,
	/* Last element */
	PE_FLAGS_COUNT
};

#ifdef TEST_BUILD
void pe_set_fn(int port, int fn);
void pe_clr_fn(int port, int fn);
int pe_chk_fn(int port, int fn);
void pe_clr_dpm_requests(int port);
#endif /* TEST_BUILD */

#endif /* __CROS_EC_USB_PE_PRIVATE_H */
