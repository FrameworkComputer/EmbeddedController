/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(tcpci_faulty_snk_emul, CONFIG_TCPCI_EMUL_LOG_LEVEL);

#include <sys/byteorder.h>
#include <zephyr.h>

#include "common.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci_partner_faulty_snk.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "usb_pd.h"

/**
 * @brief Reduce number of times to repeat action. If count reaches zero, action
 *        is removed from queue.
 *
 * @param data Pointer to USB-C malfunctioning sink device emulator data
 */
static void tcpci_faulty_snk_emul_reduce_action_count(
	struct tcpci_faulty_snk_emul_data *data)
{
	struct tcpci_faulty_snk_action *action;

	action = k_fifo_peek_head(&data->action_list);

	if (action->count == TCPCI_FAULTY_SNK_INFINITE_ACTION) {
		return;
	}

	action->count--;
	if (action->count != 0) {
		return;
	}

	/* Remove action from queue */
	k_fifo_get(&data->action_list, K_FOREVER);
}

/** Check description in emul_tcpci_partner_faulty_snk.h */
void tcpci_faulty_snk_emul_append_action(
	struct tcpci_faulty_snk_emul_data *data,
	struct tcpci_faulty_snk_action *action)
{
	k_fifo_put(&data->action_list, action);
}

/** Check description in emul_tcpci_partner_faulty_snk.h */
void tcpci_faulty_snk_emul_clear_actions_list(
	struct tcpci_faulty_snk_emul_data *data)
{
	while (!k_fifo_is_empty(&data->action_list)) {
		k_fifo_get(&data->action_list, K_FOREVER);
	}
}

/** Check description in emul_tcpci_partner_faulty_snk.h */
enum tcpci_partner_handler_res tcpci_faulty_snk_emul_handle_sop_msg(
	struct tcpci_faulty_snk_emul_data *data,
	struct tcpci_snk_emul_data *snk_data,
	struct tcpci_partner_data *common_data,
	const struct tcpci_emul_partner_ops *ops,
	const struct tcpci_emul_msg *msg)
{
	struct tcpci_faulty_snk_action *action;
	uint16_t header;

	action = k_fifo_peek_head(&data->action_list);
	header = sys_get_le16(msg->buf);

	if (PD_HEADER_CNT(header)) {
		/* Handle data message */
		switch (PD_HEADER_TYPE(header)) {
		case PD_DATA_SOURCE_CAP:
			if (action &&
			    (action->action_mask &
			     TCPCI_FAULTY_SNK_IGNORE_SRC_CAP)) {
				tcpci_faulty_snk_emul_reduce_action_count(data);
				return TCPCI_PARTNER_COMMON_MSG_HANDLED;
			}
			return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
		}
	}

	return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
}

/**
 * @brief Function called when TCPM wants to transmit message. Accept received
 *        message and generate response.
 *
 * @param emul Pointer to TCPCI emulator
 * @param ops Pointer to partner operations structure
 * @param tx_msg Pointer to TX message buffer
 * @param type Type of message
 * @param retry Count of retries
 */
static void tcpci_faulty_snk_emul_transmit_op(
	const struct emul *emul,
	const struct tcpci_emul_partner_ops *ops,
	const struct tcpci_emul_msg *tx_msg,
	enum tcpci_msg_type type,
	int retry)
{
	struct tcpci_faulty_snk_emul *faulty_snk_emul =
		CONTAINER_OF(ops, struct tcpci_faulty_snk_emul, ops);
	enum tcpci_partner_handler_res processed;
	struct tcpci_faulty_snk_action *action;
	uint16_t header;
	int ret;

	ret = k_mutex_lock(&faulty_snk_emul->common_data.transmit_mutex,
			   K_FOREVER);
	if (ret) {
		LOG_ERR("Failed to get faulty SNK mutex");
		/* Inform TCPM that message send failed */
		tcpci_partner_common_msg_handler(&faulty_snk_emul->common_data,
						 tx_msg, type,
						 TCPCI_EMUL_TX_FAILED);
		return;
	}

	action = k_fifo_peek_head(&faulty_snk_emul->data.action_list);
	header = sys_get_le16(tx_msg->buf);

	if (PD_HEADER_CNT(header) &&
	    PD_HEADER_TYPE(header) == PD_DATA_SOURCE_CAP &&
	    action &&
	    (action->action_mask & (TCPCI_FAULTY_SNK_FAIL_SRC_CAP |
				    TCPCI_FAULTY_SNK_DISCARD_SRC_CAP))) {
		if (action->action_mask & TCPCI_FAULTY_SNK_FAIL_SRC_CAP) {
			/* Fail is not sending GoodCRC from partner */
			tcpci_partner_common_msg_handler(
						&faulty_snk_emul->common_data,
						tx_msg, type,
						TCPCI_EMUL_TX_FAILED);
		} else {
			/* Discard because partner is sending message */
			tcpci_partner_common_msg_handler(
						&faulty_snk_emul->common_data,
						tx_msg, type,
						TCPCI_EMUL_TX_DISCARDED);
			tcpci_partner_send_control_msg(
						&faulty_snk_emul->common_data,
						PD_CTRL_ACCEPT, 0);
		}
		tcpci_faulty_snk_emul_reduce_action_count(
							&faulty_snk_emul->data);
		return;
	}

	/* Call common handler */
	processed = tcpci_partner_common_msg_handler(
				&faulty_snk_emul->common_data, tx_msg, type,
				TCPCI_EMUL_TX_SUCCESS);
	switch (processed) {
	case TCPCI_PARTNER_COMMON_MSG_HARD_RESET:
	case TCPCI_PARTNER_COMMON_MSG_HANDLED:
		/* Message handled nothing to do */
		k_mutex_unlock(&faulty_snk_emul->common_data.transmit_mutex);
		return;
	case TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED:
	default:
		/* Continue */
		break;
	}

	/* Handle only SOP messages */
	if (type != TCPCI_MSG_SOP) {
		k_mutex_unlock(&faulty_snk_emul->common_data.transmit_mutex);
		return;
	}

	/* Call faulty_snk specific handler */
	processed = tcpci_faulty_snk_emul_handle_sop_msg(
						&faulty_snk_emul->data,
						&faulty_snk_emul->snk_data,
						&faulty_snk_emul->common_data,
						ops, tx_msg);
	if (processed == TCPCI_PARTNER_COMMON_MSG_HANDLED) {
		k_mutex_unlock(&faulty_snk_emul->common_data.transmit_mutex);
		return;
	}

	/* Call sink specific handler */
	processed = tcpci_snk_emul_handle_sop_msg(&faulty_snk_emul->snk_data,
						  &faulty_snk_emul->common_data,
						  tx_msg);
	if (processed == TCPCI_PARTNER_COMMON_MSG_HANDLED) {
		k_mutex_unlock(&faulty_snk_emul->common_data.transmit_mutex);
		return;
	}

	/* Send reject for not handled messages (PD rev 2.0) */
	tcpci_partner_send_control_msg(&faulty_snk_emul->common_data,
				       PD_CTRL_REJECT, 0);
	k_mutex_unlock(&faulty_snk_emul->common_data.transmit_mutex);
}

/**
 * @brief Function called when TCPM consumes message. Free message that is no
 *        longer needed.
 *
 * @param emul Pointer to TCPCI emulator
 * @param ops Pointer to partner operations structure
 * @param rx_msg Message that was consumed by TCPM
 */
static void tcpci_faulty_snk_emul_rx_consumed_op(
		const struct emul *emul,
		const struct tcpci_emul_partner_ops *ops,
		const struct tcpci_emul_msg *rx_msg)
{
	struct tcpci_partner_msg *msg = CONTAINER_OF(rx_msg,
						     struct tcpci_partner_msg,
						     msg);

	tcpci_partner_free_msg(msg);
}

/**
 * @brief Function called when emulator is disconnected from TCPCI
 *
 * @param emul Pointer to TCPCI emulator
 * @param ops Pointer to partner operations structure
 */
static void tcpci_faulty_snk_emul_disconnect_op(
		const struct emul *emul,
		const struct tcpci_emul_partner_ops *ops)
{
	struct tcpci_faulty_snk_emul *faulty_snk_emul =
		CONTAINER_OF(ops, struct tcpci_faulty_snk_emul, ops);

	tcpci_partner_common_disconnect(&faulty_snk_emul->common_data);
}

/** Check description in emul_tcpci_partner_faulty_snk.h */
int tcpci_faulty_snk_emul_connect_to_tcpci(
	struct tcpci_snk_emul_data *snk_data,
	struct tcpci_partner_data *common_data,
	const struct tcpci_emul_partner_ops *ops,
	const struct emul *tcpci_emul)
{
	return tcpci_snk_emul_connect_to_tcpci(snk_data, common_data, ops,
					       tcpci_emul);
}

/** Check description in emul_tcpci_partner_faulty_snk.h */
void tcpci_faulty_snk_emul_init_data(struct tcpci_faulty_snk_emul_data *data)
{
	k_fifo_init(&data->action_list);
}

/** Check description in emul_tcpci_partner_faulty_snk.h */
void tcpci_faulty_snk_emul_init(struct tcpci_faulty_snk_emul *emul)
{
	tcpci_partner_init(&emul->common_data, tcpci_snk_emul_hard_reset,
			   &emul->snk_data);

	/* Init as sink */
	emul->common_data.data_role = PD_ROLE_DFP;
	emul->common_data.power_role = PD_ROLE_SINK;
	emul->common_data.rev = PD_REV20;

	emul->ops.transmit = tcpci_faulty_snk_emul_transmit_op;
	emul->ops.rx_consumed = tcpci_faulty_snk_emul_rx_consumed_op;
	emul->ops.control_change = NULL;
	emul->ops.disconnect = tcpci_faulty_snk_emul_disconnect_op;

	tcpci_faulty_snk_emul_init_data(&emul->data);
	tcpci_snk_emul_init_data(&emul->snk_data);
}
