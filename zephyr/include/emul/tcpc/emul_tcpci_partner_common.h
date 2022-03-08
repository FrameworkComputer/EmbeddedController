/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Common code used by TCPCI partner device emulators
 */

#ifndef __EMUL_TCPCI_PARTNER_COMMON_H
#define __EMUL_TCPCI_PARTNER_COMMON_H

#include <drivers/emul.h>
#include "emul/tcpc/emul_tcpci.h"

#include "ec_commands.h"
#include "usb_pd.h"

/**
 * @brief Common code used by TCPCI partner device emulators
 * @defgroup tcpci_partner Common code for TCPCI partner device emulators
 * @{
 *
 * Common code for TCPCI partner device emulators allows to send SOP messages
 * in generic way using optional delay.
 */

/** Common data for TCPCI partner device emulators */
struct tcpci_partner_data {
	/** Timer used to send message with delay */
	struct k_timer delayed_send;
	/** Reserved for fifo, used for scheduling messages */
	void *fifo_data;
	/** Pointer to connected TCPCI emulator */
	const struct emul *tcpci_emul;
	/** Queue for delayed messages */
	sys_slist_t to_send;
	/** Mutex for to_send queue */
	struct k_mutex to_send_mutex;
	/** Next SOP message id */
	int msg_id;
	/** Last received message id */
	int recv_msg_id;
	/** Power role (used in message header) */
	enum pd_power_role power_role;
	/** Data role (used in message header) */
	enum pd_data_role data_role;
	/** Revision (used in message header) */
	enum pd_rev_type rev;
	/**
	 * Mask for control message types that shouldn't be handled
	 * in common message handler
	 */
	uint32_t common_handler_masked;
	/**
	 * True if accept and reject messages shouldn't trigger soft reset
	 * in common message handler
	 */
	bool wait_for_response;
	/**
	 * If emulator triggers soft reset, it waits for accept. If accept
	 * doesn't arrive, hard reset is triggered.
	 */
	bool in_soft_reset;
	/**
	 * Mutex for TCPCI transmit handler. Should be used to synchronise
	 * access to partner emulator with TCPCI emulator.
	 */
	struct k_mutex transmit_mutex;
};

/** Structure of message used by TCPCI partner emulator */
struct tcpci_partner_msg {
	/** Reserved for sys_slist_* usage */
	sys_snode_t node;
	/** TCPCI emulator message */
	struct tcpci_emul_msg msg;
	/** Time when message should be sent if message is delayed */
	uint64_t time;
	/** Type of the message */
	int type;
	/** Number of data objects */
	int data_objects;
};

/** Result of common handler */
enum tcpci_partner_handler_res {
	TCPCI_PARTNER_COMMON_MSG_HANDLED,
	TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED,
	TCPCI_PARTNER_COMMON_MSG_HARD_RESET
};

/**
 * @brief Initialise common TCPCI partner emulator. Need to be called before
 *        any other function.
 *
 * @param data Pointer to USB-C charger emulator
 */
void tcpci_partner_init(struct tcpci_partner_data *data);

/**
 * @brief Allocate message with space for header and given number of data
 *        objects. Type of message is set to TCPCI_MSG_SOP by default.
 *
 * @param data_objects Number of data objects in message
 *
 * @return Pointer to new message on success
 * @return NULL on error
 */
struct tcpci_partner_msg *tcpci_partner_alloc_msg(int data_objects);

/**
 * @brief Free message's memory
 *
 * @param msg Pointer to message
 */
void tcpci_partner_free_msg(struct tcpci_partner_msg *msg);

/**
 * @brief Set header of the message
 *
 * @param data Pointer to TCPCI partner emulator
 * @param msg Pointer to message
 */
void tcpci_partner_set_header(struct tcpci_partner_data *data,
			      struct tcpci_partner_msg *msg);

/**
 * @brief Send message to TCPCI emulator or schedule message. On error message
 *        is freed.
 *
 * @param data Pointer to TCPCI partner emulator
 * @param msg Pointer to message to send
 * @param delay Optional delay
 *
 * @return 0 on success
 * @return negative on failure
 */
int tcpci_partner_send_msg(struct tcpci_partner_data *data,
			   struct tcpci_partner_msg *msg, uint64_t delay);

/**
 * @brief Send control message with optional delay
 *
 * @param data Pointer to TCPCI partner emulator
 * @param type Type of message
 * @param delay Optional delay
 *
 * @return 0 on success
 * @return -ENOMEM when there is no free memory for message
 * @return negative on failure
 */
int tcpci_partner_send_control_msg(struct tcpci_partner_data *data,
				   enum pd_ctrl_msg_type type,
				   uint64_t delay);

/**
 * @brief Send data message with optional delay. Data objects are copied to
 *        message.
 *
 * @param data Pointer to TCPCI partner emulator
 * @param type Type of message
 * @param data_obj Pointer to array of data objects
 * @param data_obj_num Number of data objects
 * @param delay Optional delay
 *
 * @return 0 on success
 * @return -ENOMEM when there is no free memory for message
 * @return negative on failure
 */
int tcpci_partner_send_data_msg(struct tcpci_partner_data *data,
				enum pd_data_msg_type type,
				uint32_t *data_obj, int data_obj_num,
				uint64_t delay);

/**
 * @brief Remove all messages that are in delayed message queue
 *
 * @param data Pointer to TCPCI partner emulator
 *
 * @return 0 on success
 * @return negative on failure
 */
int tcpci_partner_clear_msg_queue(struct tcpci_partner_data *data);

/**
 * @brief Send hard reset and set common data to state after hard reset (reset
 *        counters, flags, clear message queue)
 *
 * @param data Pointer to TCPCI partner emulator
 */
void tcpci_partner_common_send_hard_reset(struct tcpci_partner_data *data);

/**
 * @brief Send hard reset and set common data to state after soft reset (reset
 *        counters, set flags to wait for accept)
 *
 * @param data Pointer to TCPCI partner emulator
 */
void tcpci_partner_common_send_soft_reset(struct tcpci_partner_data *data);

/**
 * @brief Common handler for TCPCI messages. It handles hard reset, soft reset,
 *        repeated messages. It handles vendor defined messages by skipping
 *        them. Accept and reject messages are handled when soft reset is send.
 *        Accept/reject messages are skipped when wait_for_response flag is set.
 *        All control messages may be masked by
 *        @ref tcpci_partner_common_handler_mask_msg
 *        If @p tx_status isn't success, then all message handling is skipped.
 *
 * @param data Pointer to TCPCI partner emulator
 * @param tx_msg Message received by partner emulator
 * @param type Type of message
 * @param tx_status Status which should be returned to TCPCI emulator
 *
 * @param TCPCI_PARTNER_COMMON_MSG_HANDLED Message was handled by common code
 * @param TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED Message wasn't handled
 * @param TCPCI_PARTNER_COMMON_MSG_HARD_RESET Message was handled by sending
 *                                            hard reset
 */
enum tcpci_partner_handler_res tcpci_partner_common_msg_handler(
	struct tcpci_partner_data *data,
	const struct tcpci_emul_msg *tx_msg,
	enum tcpci_msg_type type,
	enum tcpci_emul_tx_status tx_status);


/**
 * @brief Select if @ref tcpci_partner_common_msg_handler should handle specific
 *        control message type.
 *
 * @param data Pointer to TCPCI partner emulator
 * @param type Control message to mask/unmask
 * @param enable If true message of that type is handled, if false common
 *               handler doesn't handle message of that type
 */
void tcpci_partner_common_handler_mask_msg(struct tcpci_partner_data *data,
					   enum pd_ctrl_msg_type type,
					   bool enable);

/**
 * @}
 */

#endif /* __EMUL_TCPCI_PARTNER_COMMON_H */
