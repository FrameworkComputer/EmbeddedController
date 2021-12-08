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

#include <emul.h>
#include "emul/emul_tcpci.h"

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
	/** Work used to send message with delay */
	struct k_work_delayable delayed_send;
	/** Pointer to connected TCPCI emulator */
	const struct emul *tcpci_emul;
	/** Queue for delayed messages */
	struct k_fifo to_send;
	/** Next SOP message id */
	int msg_id;
};

/** Structure of message used by TCPCI partner emulator */
struct tcpci_partner_msg {
	/** Reserved for k_fifo_* usage */
	void *fifo_reserved;
	/** TCPCI emulator message */
	struct tcpci_emul_msg msg;
	/** Time when message should be sent if message is delayed */
	uint64_t time;
};

/**
 * @brief Initialise common TCPCI partner emulator. Need to be called before
 *        any other function.
 *
 * @param data Pointer to USB-C charger emulator
 */
void tcpci_partner_init(struct tcpci_partner_data *data);

/**
 * @brief Allocate message
 *
 * @param size Size of message buffer
 *
 * @return Pointer to new message on success
 * @return NULL on error
 */
struct tcpci_partner_msg *tcpci_partner_alloc_msg(size_t size);

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
 * @param type Type of message
 * @param cnt Number of data objects
 */
void tcpci_partner_set_header(struct tcpci_partner_data *data,
			      struct tcpci_partner_msg *msg,
			      int type, int cnt);

/**
 * @brief Send message to TCPCI emulator or schedule message
 *
 * @param data Pointer to TCPCI partner emulator
 * @param msg Pointer to message to send
 * @param delay Optional delay
 *
 * @return 0 on success
 * @return -EINVAL on TCPCI emulator add RX message error
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
 * @return -EINVAL on TCPCI emulator add RX message error
 */
int tcpci_partner_send_control_msg(struct tcpci_partner_data *data,
				   enum pd_ctrl_msg_type type,
				   uint64_t delay);

/**
 * @}
 */

#endif /* __EMUL_TCPCI_PARTNER_COMMON_H */
