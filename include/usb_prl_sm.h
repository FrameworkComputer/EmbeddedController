/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Protocol Layer module */

#ifndef __CROS_EC_USB_PRL_H
#define __CROS_EC_USB_PRL_H
#include "common.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

enum prl_tx_state_id {
	PRL_TX_PHY_LAYER_RESET,
	PRL_TX_WAIT_FOR_MESSAGE_REQUEST,
	PRL_TX_LAYER_RESET_FOR_TRANSMIT,
	PRL_TX_CONSTRUCT_MESSAGE,
	PRL_TX_WAIT_FOR_PHY_RESPONSE,
	PRL_TX_MATCH_MESSAGE_ID,
	PRL_TX_MESSAGE_SENT,
	PRL_TX_CHECK_RETRY_COUNTER,
	PRL_TX_TRANSMISSION_ERROR,
	PRL_TX_DISCARD_MESSAGE,

	PRL_TX_SRC_SINK_TX,
	PRL_TX_SRC_SOURCE_TX,
	PRL_TX_SRC_PENDING,

	PRL_TX_SNK_START_OF_AMS,
	PRL_TX_SNK_PENDING,
};

enum prl_hr_state_id {
	PRL_HR_WAIT_FOR_REQUEST,
	PRL_HR_RESET_LAYER,
	PRL_HR_INDICATE_HARD_RESET,
	PRL_HR_WAIT_FOR_PHY_HARD_RESET_COMPLETE,
	PRL_HR_PHY_HARD_RESET_REQUESTED,
	PRL_HR_WAIT_FOR_PE_HARD_RESET_COMPLETE,
	PRL_HR_PE_HARD_RESET_COMPLETE,
};

enum rch_state_id {
	RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER,
	RCH_PASS_UP_MESSAGE,
	RCH_PROCESSING_EXTENDED_MESSAGE,
	RCH_REQUESTING_CHUNK,
	RCH_WAITING_CHUNK,
	RCH_REPORT_ERROR,
};

enum tch_state_id {
	TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE,
	TCH_PASS_DOWN_MESSAGE,
	TCH_WAIT_FOR_TRANSMISSION_COMPLETE,
	TCH_MESSAGE_SENT,
	TCH_PREPARE_TO_SEND_CHUNKED_MESSAGE,
	TCH_CONSTRUCT_CHUNKED_MESSAGE,
	TCH_SENDING_CHUNKED_MESSAGE,
	TCH_WAIT_CHUNK_REQUEST,
	TCH_MESSAGE_RECEIVED,
	TCH_REPORT_ERROR,
};

/*
 * Number of times the Protocol Layer will try to transmit a message
 * before giving up and signaling an error
 */
#define N_RETRY_COUNT 2

/**
 * Initialize the Protocol Layer State Machine
 *
 * @param port USB-C port number
 */
void prl_init(int port);

/**
 * Resets the Protocol Layer State Machine
 *
 * @param port USB-C port number
 */
void prl_reset(int port);

/**
 * Get Chunked Rx State Machine state id
 *
 * @param port USB-C port number
 * @return id
 */
enum rch_state_id get_rch_state_id(int port);

/**
 * Get Chunked Tx State Machine state id
 *
 * @param port USB-C port number
 * @return id
 */
enum tch_state_id get_tch_state_id(int port);

/**
 * Get Message Transmission State Machine state id
 *
 * @param port USB-C port number
 * @return id
 */
enum prl_tx_state_id get_prl_tx_state_id(int port);

/**
 * Get Hard Reset State Machine state id
 *
 * @param port USB-C port number
 * @return id
 */
enum prl_hr_state_id get_prl_hr_state_id(int port);

/**
 * Returns the state of the PRL state machine
 * @return SM_INIT for initializing
 *         SM_RUN for running
 *         SM_PAUSED for paused
 */
enum sm_local_state prl_get_local_state(int port);

/**
 * Runs the Protocol Layer State Machine
 *
 * @param port USB-C port number
 * @param evt  system event, ie: PD_EVENT_RX
 * @param en   0 to disable the machine, 1 to enable the machine
 */
void protocol_layer(int port, int evt, int en);

/**
 * Set the PD revision
 *
 * @param port USB-C port number
 * @param rev revision
 */
void prl_set_rev(int port, enum pd_rev_type rev);

/**
 * Get the PD revision
 *
 * @param port USB-C port number
 * @return pd rev
 */
enum pd_rev_type prl_get_rev(int port);

/**
 * Sends a PD control message
 *
 * @param port USB-C port number
 * @param type Transmit type
 * @param msg  Control message type
 * @return 0 on EC_SUCCESS, else EC_ERROR_BUSY
 */
void prl_send_ctrl_msg(int port, enum tcpm_transmit_type type,
	enum pd_ctrl_msg_type msg);

/**
 * Sends a PD data message
 *
 * @param port USB-C port number
 * @param type Transmit type
 * @param msg  Data message type
 * @return 0 on EC_SUCCESS, else EC_ERROR_BUSY
 */
void prl_send_data_msg(int port, enum tcpm_transmit_type type,
	enum pd_data_msg_type msg);

/**
 * Sends a PD extended data message
 *
 * @param port USB-C port number
 * @param type Transmit type
 * @param msg  Extended data message type
 * @return 0 on EC_SUCCESS, else EC_ERROR_BUSY
 */
void prl_send_ext_data_msg(int port, enum tcpm_transmit_type type,
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
 * Informs the Protocol Layer to start an Atomic Message Sequence
 *
 * @param port USB-C port number
 */
void prl_start_ams(int port);

/**
 * Informs the Protocol Layer to end an Atomic Message Sequence
 *
 * @param port USB-C port number
 */
void prl_end_ams(int port);

#endif /* __CROS_EC_USB_PRL_H */

