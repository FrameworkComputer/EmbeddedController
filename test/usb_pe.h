/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB PE module.
 */
#ifndef __CROS_TEST_USB_PE_H
#define __CROS_TEST_USB_PE_H

#include "common.h"

/*
 * Test references to port
 */
#define PORT0 0

/*
 * Parameters for pe_run
 *
 * pe_run(port, evt, enable)
 *    evt - currently ignored in the implementation
 *    enable - 0 Disable/1 Enable
 */
#define EVT_IGNORED 0

#define ENABLED 1
#define DISABLED 0

/**
 * usb_pe_drp_sm.c locally defined.  If it changes there, it must
 * be changed here as well.
 */

/* Policy Engine Layer Flags */
/* At least one successful PD communication packet received from port partner */
#define PE_FLAGS_PD_CONNECTION               BIT(0)
#define PE_FLAGS_ACCEPT                      BIT(1) /* Got accept message */
#define PE_FLAGS_PS_READY                    BIT(2) /* Power Supply Ready */
#define PE_FLAGS_PROTOCOL_ERROR              BIT(3) /* Received Protocol Err */
#define PE_FLAGS_MODAL_OPERATION             BIT(4) /* Modal operation set */
#define PE_FLAGS_TX_COMPLETE                 BIT(5) /* Transmit complete */
#define PE_FLAGS_MSG_RECEIVED                BIT(6) /* Messaged Received */
#define PE_FLAGS_HARD_RESET_PENDING          BIT(7) /* Hard reset pending */
#define PE_FLAGS_WAIT                        BIT(8) /* Wait before msg resend */
#define PE_FLAGS_EXPLICIT_CONTRACT           BIT(9) /* PD contract in place */
#define PE_FLAGS_SNK_WAIT_CAP_TIMEOUT        BIT(10)/* Snk caps timeout */
/* Power Supply transition timeout */
#define PE_FLAGS_PS_TRANSITION_TIMEOUT       BIT(11)
/* Interruptible Atomic Message Sequence */
#define PE_FLAGS_INTERRUPTIBLE_AMS           BIT(12)
/* Power Supply reset complete */
#define PE_FLAGS_PS_RESET_COMPLETE           BIT(13)
/* When set, triggers a Structured Vendor Defined Message transmission */
#define PE_FLAGS_SEND_SVDM                   BIT(14)
#define PE_FLAGS_VCONN_SWAP_COMPLETE         BIT(15)/* VCONN Swap complete */
/* When set, no more discover identity messages are sent to the port partner */
#define PE_FLAGS_DISCOVER_PORT_IDENTITY_DONE BIT(16)
/* Starts the Swap Source Start timer when set */
#define PE_FLAGS_RUN_SOURCE_START_TIMER      BIT(17)
/* Set during the port discovery if the port partner replied with busy */
#define PE_FLAGS_VDM_REQUEST_BUSY            BIT(18)
/* Set during the port discovery if the port partner replied with nak */
#define PE_FLAGS_VDM_REQUEST_NAKED           BIT(19)
#define PE_FLAGS_FAST_ROLE_SWAP_PATH         BIT(20)/* FRS/PRS Exec Path */
#define PE_FLAGS_FAST_ROLE_SWAP_ENABLED      BIT(21)/* FRS Listening State */
#define PE_FLAGS_FAST_ROLE_SWAP_SIGNALED     BIT(22)/* FRS PPC/TCPC Signal */
/* When set, no more discover identity messages are sent to SOP' */
#define PE_FLAGS_DISCOVER_VDM_IDENTITY_DONE  BIT(23)

/* List of all Policy Engine level states */
enum usb_pe_state {
	/* Normal States */
	PE_SRC_STARTUP,
	PE_SRC_DISCOVERY,
	PE_SRC_SEND_CAPABILITIES,
	PE_SRC_NEGOTIATE_CAPABILITY,
	PE_SRC_TRANSITION_SUPPLY,
	PE_SRC_READY,
	PE_SRC_DISABLED,
	PE_SRC_CAPABILITY_RESPONSE,
	PE_SRC_HARD_RESET,
	PE_SRC_HARD_RESET_RECEIVED,
	PE_SRC_TRANSITION_TO_DEFAULT,
	PE_SRC_VDM_IDENTITY_REQUEST,
	PE_SNK_STARTUP,
	PE_SNK_DISCOVERY,
	PE_SNK_WAIT_FOR_CAPABILITIES,
	PE_SNK_EVALUATE_CAPABILITY,
	PE_SNK_SELECT_CAPABILITY,
	PE_SNK_READY,
	PE_SNK_HARD_RESET,
	PE_SNK_TRANSITION_TO_DEFAULT,
	PE_SNK_GIVE_SINK_CAP,
	PE_SNK_GET_SOURCE_CAP,
	PE_SNK_TRANSITION_SINK,
	PE_SEND_SOFT_RESET,
	PE_SOFT_RESET,
	PE_SEND_NOT_SUPPORTED,
	PE_SRC_PING,
	PE_GIVE_BATTERY_CAP,
	PE_GIVE_BATTERY_STATUS,
	PE_DRS_EVALUATE_SWAP,
	PE_DRS_CHANGE,
	PE_DRS_SEND_SWAP,
	PE_PRS_SRC_SNK_EVALUATE_SWAP,
	PE_PRS_SRC_SNK_TRANSITION_TO_OFF,
	PE_PRS_SRC_SNK_WAIT_SOURCE_ON,
	PE_PRS_SRC_SNK_SEND_SWAP,
	PE_PRS_SNK_SRC_EVALUATE_SWAP,
	PE_PRS_SNK_SRC_TRANSITION_TO_OFF,
	PE_PRS_SNK_SRC_ASSERT_RP,
	PE_PRS_SNK_SRC_SOURCE_ON,
	PE_PRS_SNK_SRC_SEND_SWAP,
	PE_FRS_SNK_SRC_START_AMS,
	PE_VCS_EVALUATE_SWAP,
	PE_VCS_SEND_SWAP,
	PE_VCS_WAIT_FOR_VCONN_SWAP,
	PE_VCS_TURN_ON_VCONN_SWAP,
	PE_VCS_TURN_OFF_VCONN_SWAP,
	PE_VCS_SEND_PS_RDY_SWAP,
	PE_DO_PORT_DISCOVERY,
	PE_VDM_REQUEST,
	PE_VDM_ACKED,
	PE_VDM_RESPONSE,
	PE_HANDLE_CUSTOM_VDM_REQUEST,
	PE_WAIT_FOR_ERROR_RECOVERY,
	PE_BIST,
	PE_DR_SNK_GET_SINK_CAP,

	/* Super States */
	PE_PRS_FRS_SHARED,
};

void set_state_pe(const int port, const enum usb_pe_state new_state);
enum usb_pe_state get_state_pe(const int port);

void pe_set_flag(int port, int flag);
void pe_clr_flag(int port, int flag);
int pe_chk_flag(int port, int flag);
int pe_get_all_flags(int port);
void pe_set_all_flags(int port, int flags);

#endif /* __CROS_TEST_USB_PE_H */
