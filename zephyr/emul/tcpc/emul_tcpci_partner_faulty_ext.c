/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci_partner_faulty_ext.h"
#include "usb_pd.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(tcpci_faulty_ext, CONFIG_TCPCI_EMUL_LOG_LEVEL);

/**
 * @brief Reduce number of times to repeat action. If count reaches zero, action
 *        is removed from queue.
 *
 * @param data Pointer to USB-C malfunctioning device extension data
 */
static void
tcpci_faulty_ext_reduce_action_count(struct tcpci_faulty_ext_data *data)
{
	struct tcpci_faulty_ext_action *action;
	void *buf;

	action = k_fifo_peek_head(&data->action_list);

	if (action->count == TCPCI_FAULTY_EXT_INFINITE_ACTION) {
		return;
	}

	action->count--;
	if (action->count != 0) {
		return;
	}

	/* Remove action from queue */
	buf = k_fifo_get(&data->action_list, K_FOREVER);
}

void tcpci_faulty_ext_append_action(struct tcpci_faulty_ext_data *data,
				    struct tcpci_faulty_ext_action *action)
{
	k_fifo_put(&data->action_list, action);
}

void tcpci_faulty_ext_clear_actions_list(struct tcpci_faulty_ext_data *data)
{
	void *buf;
	while (!k_fifo_is_empty(&data->action_list)) {
		buf = k_fifo_get(&data->action_list, K_FOREVER);
	}
}

/**
 * @brief Handle SOP messages as TCPCI malfunctioning device
 *
 * @param ext Pointer to USB-C malfunctioning device emulator extension
 * @param common_data Pointer to USB-C device emulator common data
 * @param msg Pointer to received message
 *
 * @return TCPCI_PARTNER_COMMON_MSG_HANDLED Message was handled
 * @return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED Message wasn't handled
 */
static enum tcpci_partner_handler_res
tcpci_faulty_ext_handle_sop_msg(struct tcpci_partner_extension *ext,
				struct tcpci_partner_data *common_data,
				const struct tcpci_emul_msg *msg)
{
	struct tcpci_faulty_ext_data *data =
		CONTAINER_OF(ext, struct tcpci_faulty_ext_data, ext);
	struct tcpci_faulty_ext_action *action;
	uint16_t header;

	action = k_fifo_peek_head(&data->action_list);
	header = sys_get_le16(msg->buf);

	if (action == NULL) {
		/* No faulty action, so send GoodCRC */
		tcpci_emul_partner_msg_status(common_data->tcpci_emul,
					      TCPCI_EMUL_TX_SUCCESS);

		return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
	}

	if (PD_HEADER_CNT(header)) {
		/* Handle data message */
		switch (PD_HEADER_TYPE(header)) {
		case PD_DATA_SOURCE_CAP:
			if (action->action_mask &
			    TCPCI_FAULTY_EXT_FAIL_SRC_CAP) {
				/* Fail is not sending GoodCRC from partner */
				tcpci_partner_received_msg_status(
					common_data, TCPCI_EMUL_TX_FAILED);
				tcpci_faulty_ext_reduce_action_count(data);
				return TCPCI_PARTNER_COMMON_MSG_HANDLED;
			}
			if (action->action_mask &
			    TCPCI_FAULTY_EXT_DISCARD_SRC_CAP) {
				/* Discard because partner is sending message */
				tcpci_partner_received_msg_status(
					common_data, TCPCI_EMUL_TX_DISCARDED);
				tcpci_partner_send_control_msg(
					common_data, PD_CTRL_ACCEPT, 0);
				tcpci_faulty_ext_reduce_action_count(data);
				return TCPCI_PARTNER_COMMON_MSG_HANDLED;
			}
			if (action->action_mask &
			    TCPCI_FAULTY_EXT_IGNORE_SRC_CAP) {
				/* Send only GoodCRC */
				tcpci_partner_received_msg_status(
					common_data, TCPCI_EMUL_TX_SUCCESS);
				tcpci_faulty_ext_reduce_action_count(data);
				return TCPCI_PARTNER_COMMON_MSG_HANDLED;
			}
		}
	} else {
		switch (PD_HEADER_TYPE(header)) {
		case PD_CTRL_FR_SWAP:
			if (action->action_mask &
			    TCPCI_FAULTY_EXT_IGNORE_FR_SWAP) {
				/* Send only GoodCRC */
				tcpci_partner_received_msg_status(
					common_data, TCPCI_EMUL_TX_SUCCESS);
				tcpci_faulty_ext_reduce_action_count(data);
				return TCPCI_PARTNER_COMMON_MSG_HANDLED;
			} else if (action->action_mask &
				   TCPCI_FAULTY_EXT_REJECT_FR_SWAP) {
				tcpci_partner_send_control_msg(
					common_data, PD_CTRL_REJECT, 0);
				tcpci_faulty_ext_reduce_action_count(data);
				return TCPCI_PARTNER_COMMON_MSG_HANDLED;
			} else if (action->action_mask &
				   TCPCI_FAULTY_EXT_ACCEPT_FR_SWAP_TIMEOUT) {
				/* Send Accept, but wait too long to do it. The
				 * maximum allowed value of tSenderResponse is
				 * 33 ms, so wait 35 ms.
				 */
				tcpci_partner_send_control_msg(
					common_data, PD_CTRL_ACCEPT, 35);
				tcpci_faulty_ext_reduce_action_count(data);
				return TCPCI_PARTNER_COMMON_MSG_HANDLED;
			}
		}
	}

	/*
	 * Send GoodCRC for all unhandled messages, since we disabled it in
	 * common handler
	 */
	tcpci_partner_received_msg_status(common_data, TCPCI_EMUL_TX_SUCCESS);

	return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
}

/** USB-C malfunctioning device extension callbacks */
struct tcpci_partner_extension_ops tcpci_faulty_ext_ops = {
	.sop_msg_handler = tcpci_faulty_ext_handle_sop_msg,
	.hard_reset = NULL,
	.soft_reset = NULL,
	.disconnect = NULL,
	.connect = NULL,
};

struct tcpci_partner_extension *
tcpci_faulty_ext_init(struct tcpci_faulty_ext_data *data,
		      struct tcpci_partner_data *common_data,
		      struct tcpci_partner_extension *ext)
{
	struct tcpci_partner_extension *faulty_ext = &data->ext;

	k_fifo_init(&data->action_list);
	common_data->send_goodcrc = false;

	faulty_ext->next = ext;
	faulty_ext->ops = &tcpci_faulty_ext_ops;

	return faulty_ext;
}
