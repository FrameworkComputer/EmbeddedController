/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(tcpci_drp_emul, CONFIG_TCPCI_EMUL_LOG_LEVEL);

#include <sys/byteorder.h>
#include <zephyr.h>

#include "common.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci_partner_drp.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "tcpm/tcpci.h"
#include "usb_pd.h"

/** Check description in emul_tcpci_partner_drp.h */
enum tcpci_partner_handler_res tcpci_drp_emul_handle_sop_msg(
	struct tcpci_drp_emul_data *data,
	struct tcpci_src_emul_data *src_data,
	struct tcpci_snk_emul_data *snk_data,
	struct tcpci_partner_data *common_data,
	const struct tcpci_emul_partner_ops *ops,
	const struct tcpci_emul_msg *msg)
{
	uint16_t pwr_status;
	uint16_t header;

	header = sys_get_le16(msg->buf);

	if (PD_HEADER_CNT(header)) {
		/* Handle data message */
		switch (PD_HEADER_TYPE(header)) {
		case PD_DATA_REQUEST:
			if (data->sink) {
				/* As sink we shouldn't accept request */
				tcpci_partner_send_control_msg(common_data,
							       PD_CTRL_REJECT,
							       0);
				return TCPCI_PARTNER_COMMON_MSG_HANDLED;
			}
			/* As source, let source handler to handle this */
			return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
		case PD_DATA_SOURCE_CAP:
			if (!data->sink) {
				/* As source we shouldn't respond */
				return TCPCI_PARTNER_COMMON_MSG_HANDLED;
			}
			/* As sink, let sink handler to handle this */
			return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
		}
	} else {
		/* Handle control message */
		switch (PD_HEADER_TYPE(header)) {
		case PD_CTRL_PR_SWAP:
			tcpci_partner_send_control_msg(common_data,
						       PD_CTRL_ACCEPT,
						       0);
			data->in_pwr_swap = true;
			return TCPCI_PARTNER_COMMON_MSG_HANDLED;
		case PD_CTRL_PS_RDY:
			if (!data->in_pwr_swap) {
				return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
			}
			data->in_pwr_swap = false;

			/* Reset counters */
			common_data->msg_id = 0;
			common_data->recv_msg_id = -1;

			/* Perform power role swap */
			if (!data->sink) {
				/* Disable VBUS if emulator was source */
				tcpci_emul_get_reg(common_data->tcpci_emul,
						   TCPC_REG_POWER_STATUS,
						   &pwr_status);
				pwr_status &= ~TCPC_REG_POWER_STATUS_VBUS_PRES;
				tcpci_emul_set_reg(common_data->tcpci_emul,
						   TCPC_REG_POWER_STATUS,
						   pwr_status);
				/* Reconnect as sink */
				data->sink = true;
				common_data->power_role = PD_ROLE_SINK;
			} else {
				/* Reconnect as source */
				data->sink = false;
				common_data->power_role = PD_ROLE_SOURCE;
			}
			tcpci_partner_send_control_msg(common_data,
						       PD_CTRL_PS_RDY, 0);
			/* Reconnect to TCPCI emulator */
			tcpci_drp_emul_connect_to_tcpci(
					data, src_data, snk_data, common_data,
					ops, common_data->tcpci_emul);

			return TCPCI_PARTNER_COMMON_MSG_HANDLED;
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
static void tcpci_drp_emul_transmit_op(const struct emul *emul,
				       const struct tcpci_emul_partner_ops *ops,
				       const struct tcpci_emul_msg *tx_msg,
				       enum tcpci_msg_type type,
				       int retry)
{
	struct tcpci_drp_emul *drp_emul =
		CONTAINER_OF(ops, struct tcpci_drp_emul, ops);
	enum tcpci_partner_handler_res processed;
	uint16_t header;
	int ret;

	ret = k_mutex_lock(&drp_emul->common_data.transmit_mutex, K_FOREVER);
	if (ret) {
		LOG_ERR("Failed to get DRP mutex");
		/* Inform TCPM that message send failed */
		tcpci_partner_common_msg_handler(&drp_emul->common_data,
						 tx_msg, type,
						 TCPCI_EMUL_TX_FAILED);
		return;
	}

	header = sys_get_le16(tx_msg->buf);

	/* Call common handler */
	processed = tcpci_partner_common_msg_handler(&drp_emul->common_data,
						     tx_msg, type,
						     TCPCI_EMUL_TX_SUCCESS);
	switch (processed) {
	case TCPCI_PARTNER_COMMON_MSG_HARD_RESET:
		/* Handle hard reset */
		if (!drp_emul->data.sink) {
			/* As source, advertise capabilities after 15 ms */
			tcpci_src_emul_send_capability_msg(
							&drp_emul->src_data,
							&drp_emul->common_data,
							15);
		}
		drp_emul->snk_data.wait_for_ps_rdy = false;
		drp_emul->snk_data.pd_completed = false;
		k_mutex_unlock(&drp_emul->common_data.transmit_mutex);
		return;
	case TCPCI_PARTNER_COMMON_MSG_HANDLED:
		if (!drp_emul->data.sink && PD_HEADER_CNT(header) == 0 &&
		    PD_HEADER_TYPE(header) == PD_CTRL_SOFT_RESET) {
			/*
			 * As source, advertise capabilities after 15 ms after
			 * soft reset
			 */
			tcpci_src_emul_send_capability_msg(
							&drp_emul->src_data,
							&drp_emul->common_data,
							15);
		}
		/* Message handled nothing to do */
		k_mutex_unlock(&drp_emul->common_data.transmit_mutex);
		return;
	case TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED:
	default:
		/* Continue */
		break;
	}

	/* Handle only SOP messages */
	if (type != TCPCI_MSG_SOP) {
		k_mutex_unlock(&drp_emul->common_data.transmit_mutex);
		return;
	}

	/* Call drp specific handler */
	processed = tcpci_drp_emul_handle_sop_msg(&drp_emul->data,
						  &drp_emul->src_data,
						  &drp_emul->snk_data,
						  &drp_emul->common_data,
						  ops, tx_msg);
	if (processed == TCPCI_PARTNER_COMMON_MSG_HANDLED) {
		k_mutex_unlock(&drp_emul->common_data.transmit_mutex);
		return;
	}

	/* Call source specific handler */
	processed = tcpci_src_emul_handle_sop_msg(&drp_emul->src_data,
						  &drp_emul->common_data,
						  tx_msg);
	if (processed == TCPCI_PARTNER_COMMON_MSG_HANDLED) {
		k_mutex_unlock(&drp_emul->common_data.transmit_mutex);
		return;
	}

	/* Call sink specific handler */
	processed = tcpci_snk_emul_handle_sop_msg(&drp_emul->snk_data,
						  &drp_emul->common_data,
						  tx_msg);
	if (processed == TCPCI_PARTNER_COMMON_MSG_HANDLED) {
		k_mutex_unlock(&drp_emul->common_data.transmit_mutex);
		return;
	}

	/* Send reject for not handled messages (PD rev 2.0) */
	tcpci_partner_send_control_msg(&drp_emul->common_data,
				       PD_CTRL_REJECT, 0);
	k_mutex_unlock(&drp_emul->common_data.transmit_mutex);
}

/**
 * @brief Function called when TCPM consumes message. Free message that is no
 *        longer needed.
 *
 * @param emul Pointer to TCPCI emulator
 * @param ops Pointer to partner operations structure
 * @param rx_msg Message that was consumed by TCPM
 */
static void tcpci_drp_emul_rx_consumed_op(
		const struct emul *emul,
		const struct tcpci_emul_partner_ops *ops,
		const struct tcpci_emul_msg *rx_msg)
{
	struct tcpci_partner_msg *msg = CONTAINER_OF(rx_msg,
						     struct tcpci_partner_msg,
						     msg);

	tcpci_partner_free_msg(msg);
}

/** Check description in emul_tcpci_partner_drp.h */
int tcpci_drp_emul_connect_to_tcpci(struct tcpci_drp_emul_data *data,
				    struct tcpci_src_emul_data *src_data,
				    struct tcpci_snk_emul_data *snk_data,
				    struct tcpci_partner_data *common_data,
				    const struct tcpci_emul_partner_ops *ops,
				    const struct emul *tcpci_emul)
{
	if (data->sink) {
		return tcpci_snk_emul_connect_to_tcpci(snk_data, common_data,
						       ops, tcpci_emul);
	}

	return tcpci_src_emul_connect_to_tcpci(src_data, common_data,
					       ops, tcpci_emul);
}

/** Check description in emul_tcpci_partner_drp.h */
void tcpci_drp_emul_init(struct tcpci_drp_emul *emul)
{
	tcpci_partner_init(&emul->common_data);

	/* By default init as sink */
	emul->common_data.data_role = PD_ROLE_DFP;
	emul->common_data.power_role = PD_ROLE_SINK;
	emul->common_data.rev = PD_REV20;

	emul->ops.transmit = tcpci_drp_emul_transmit_op;
	emul->ops.rx_consumed = tcpci_drp_emul_rx_consumed_op;
	emul->ops.control_change = NULL;

	emul->data.sink = true;
	emul->data.in_pwr_swap = false;
	tcpci_src_emul_init_data(&emul->src_data);
	tcpci_snk_emul_init_data(&emul->snk_data);

	/* Add dual role bit to sink and source PDOs */
	emul->src_data.pdo[0] |= PDO_FIXED_DUAL_ROLE;
	emul->snk_data.pdo[0] |= PDO_FIXED_DUAL_ROLE;
}
