/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(tcpci_src_emul, CONFIG_TCPCI_EMUL_LOG_LEVEL);

#include <sys/byteorder.h>
#include <zephyr.h>

#include "common.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "emul/tcpc/emul_tcpci.h"
#include "usb_pd.h"

/** Check description in emul_tcpci_partner_src.h */
int tcpci_src_emul_send_capability_msg(struct tcpci_src_emul_data *data,
				       struct tcpci_partner_data *common_data,
				       uint64_t delay)
{
	int pdos;

	/* Find number of PDOs */
	for (pdos = 0; pdos < PDO_MAX_OBJECTS; pdos++) {
		if (data->pdo[pdos] == 0) {
			break;
		}
	}

	return tcpci_partner_send_data_msg(common_data,
					   PD_DATA_SOURCE_CAP,
					   data->pdo, pdos, delay);
}

/** Check description in emul_tcpci_partner_src.h */
enum tcpci_partner_handler_res tcpci_src_emul_handle_sop_msg(
	struct tcpci_src_emul_data *data,
	struct tcpci_partner_data *common_data,
	const struct tcpci_emul_msg *msg)
{
	uint16_t header;

	header = sys_get_le16(msg->buf);

	if (PD_HEADER_CNT(header)) {
		/* Handle data message */
		switch (PD_HEADER_TYPE(header)) {
		case PD_DATA_REQUEST:
			tcpci_partner_send_control_msg(common_data,
						       PD_CTRL_ACCEPT, 0);
			/* PS ready after 15 ms */
			tcpci_partner_send_control_msg(common_data,
						       PD_CTRL_PS_RDY, 15);
			return TCPCI_PARTNER_COMMON_MSG_HANDLED;
		default:
			return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
		}
	} else {
		/* Handle control message */
		switch (PD_HEADER_TYPE(header)) {
		case PD_CTRL_GET_SOURCE_CAP:
			tcpci_src_emul_send_capability_msg(data, common_data,
							   0);
			return TCPCI_PARTNER_COMMON_MSG_HANDLED;
		case PD_CTRL_SOFT_RESET:
			/* Send capability after 15 ms to establish PD again */
			tcpci_src_emul_send_capability_msg(data, common_data,
							   15);
			return TCPCI_PARTNER_COMMON_MSG_HANDLED;
		default:
			return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
		}
	}
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
static void tcpci_src_emul_transmit_op(const struct emul *emul,
				       const struct tcpci_emul_partner_ops *ops,
				       const struct tcpci_emul_msg *tx_msg,
				       enum tcpci_msg_type type,
				       int retry)
{
	struct tcpci_src_emul *src_emul =
		CONTAINER_OF(ops, struct tcpci_src_emul, ops);
	enum tcpci_partner_handler_res processed;
	uint16_t header;
	int ret;

	ret = k_mutex_lock(&src_emul->common_data.transmit_mutex, K_FOREVER);
	if (ret) {
		LOG_ERR("Failed to get SRC mutex");
		/* Inform TCPM that message send failed */
		tcpci_partner_common_msg_handler(&src_emul->common_data,
						 tx_msg, type,
						 TCPCI_EMUL_TX_FAILED);
		return;
	}

	processed = tcpci_partner_common_msg_handler(&src_emul->common_data,
						     tx_msg, type,
						     TCPCI_EMUL_TX_SUCCESS);
	/* Handle hard reset */
	if (processed == TCPCI_PARTNER_COMMON_MSG_HARD_RESET) {
		/* Send capability after 15 ms to establish PD again */
		tcpci_src_emul_send_capability_msg(&src_emul->data,
						   &src_emul->common_data, 15);
		k_mutex_unlock(&src_emul->common_data.transmit_mutex);
		return;
	}

	/* Handle only SOP messages */
	if (type != TCPCI_MSG_SOP) {
		k_mutex_unlock(&src_emul->common_data.transmit_mutex);
		return;
	}

	header = sys_get_le16(tx_msg->buf);
	if (processed == TCPCI_PARTNER_COMMON_MSG_HANDLED &&
	    !(PD_HEADER_CNT(header) == 0 &&
	      PD_HEADER_TYPE(header) == PD_CTRL_SOFT_RESET)) {
		/*
		 * Only soft reset requires additional handling after
		 * common handler
		 */
		k_mutex_unlock(&src_emul->common_data.transmit_mutex);
		return;
	}

	/* Call source specific handler */
	processed = tcpci_src_emul_handle_sop_msg(&src_emul->data,
						  &src_emul->common_data,
						  tx_msg);
	if (processed == TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED) {
		/* Send reject for not handled messages (PD rev 2.0) */
		tcpci_partner_send_control_msg(&src_emul->common_data,
					       PD_CTRL_REJECT, 0);
	}
	k_mutex_unlock(&src_emul->common_data.transmit_mutex);
}

/**
 * @brief Function called when TCPM consumes message. Free message that is no
 *        longer needed.
 *
 * @param emul Pointer to TCPCI emulator
 * @param ops Pointer to partner operations structure
 * @param rx_msg Message that was consumed by TCPM
 */
static void tcpci_src_emul_rx_consumed_op(
		const struct emul *emul,
		const struct tcpci_emul_partner_ops *ops,
		const struct tcpci_emul_msg *rx_msg)
{
	struct tcpci_partner_msg *msg = CONTAINER_OF(rx_msg,
						     struct tcpci_partner_msg,
						     msg);

	tcpci_partner_free_msg(msg);
}

/** Check description in emul_tcpci_partner_src.h */
int tcpci_src_emul_connect_to_tcpci(struct tcpci_src_emul_data *data,
				    struct tcpci_partner_data *common_data,
				    const struct tcpci_emul_partner_ops *ops,
				    const struct emul *tcpci_emul)
{
	int ec;

	tcpci_emul_set_partner_ops(tcpci_emul, ops);
	ec = tcpci_emul_connect_partner(tcpci_emul, PD_ROLE_SOURCE,
					TYPEC_CC_VOLT_RP_3_0,
					TYPEC_CC_VOLT_OPEN, POLARITY_CC1);
	if (ec) {
		return ec;
	}

	common_data->tcpci_emul = tcpci_emul;

	return tcpci_src_emul_send_capability_msg(data, common_data, 0);
}

#define PDO_FIXED_FLAGS_MASK						\
	(PDO_FIXED_DUAL_ROLE | PDO_FIXED_UNCONSTRAINED |		\
	 PDO_FIXED_COMM_CAP | PDO_FIXED_DATA_SWAP)

/** Check description in emul_tcpci_parnter_src.h */
enum check_pdos_res tcpci_src_emul_check_pdos(struct tcpci_src_emul_data *data)
{
	int volt_i_min;
	int volt_i_max;
	int volt_min;
	int volt_max;
	int i;

	/* Check that first PDO is fixed 5V */
	if ((data->pdo[0] & PDO_TYPE_MASK) != PDO_TYPE_FIXED ||
	    PDO_FIXED_VOLTAGE(data->pdo[0]) != 5000) {
		return TCPCI_SRC_EMUL_FIRST_PDO_NO_FIXED_5V;
	}

	/* Check fixed PDOs are before other types and are in correct order */
	for (i = 1, volt_min = -1;
	     i < PDO_MAX_OBJECTS && data->pdo[i] != 0 &&
	     (data->pdo[i] & PDO_TYPE_MASK) != PDO_TYPE_FIXED;
	     i++) {
		volt_i_min = PDO_FIXED_VOLTAGE(data->pdo[i]);
		/* Each voltage should be only once */
		if (volt_i_min == volt_min || volt_i_min == 5000) {
			return TCPCI_SRC_EMUL_FIXED_VOLT_REPEATED;
		}
		/* Check that voltage is increasing in next PDO */
		if (volt_i_min < volt_min) {
			return TCPCI_SRC_EMUL_FIXED_VOLT_NOT_IN_ORDER;
		}
		/* Check that fixed PDOs (except first) have cleared flags */
		if (data->pdo[i] & PDO_FIXED_FLAGS_MASK) {
			return TCPCI_SRC_EMUL_NON_FIRST_PDO_FIXED_FLAGS;
		}
		/* Save current voltage */
		volt_min = volt_i_min;
	}

	/* Check battery PDOs are before variable type and are in order */
	for (volt_min = -1, volt_max = -1;
	     i < PDO_MAX_OBJECTS && data->pdo[i] != 0 &&
	     (data->pdo[i] & PDO_TYPE_MASK) != PDO_TYPE_BATTERY;
	     i++) {
		volt_i_min = PDO_BATT_MIN_VOLTAGE(data->pdo[i]);
		volt_i_max = PDO_BATT_MAX_VOLTAGE(data->pdo[i]);
		/* Each voltage range should be only once */
		if (volt_i_min == volt_min && volt_i_max == volt_max) {
			return TCPCI_SRC_EMUL_BATT_VOLT_REPEATED;
		}
		/*
		 * Lower minimal voltage should be first, than lower maximal
		 * voltage.
		 */
		if (volt_i_min < volt_min ||
		    (volt_i_min == volt_min && volt_i_max < volt_max)) {
			return TCPCI_SRC_EMUL_BATT_VOLT_NOT_IN_ORDER;
		}
		/* Save current voltage */
		volt_min = volt_i_min;
		volt_max = volt_i_max;
	}

	/* Check variable PDOs are last and are in correct order */
	for (volt_min = -1, volt_max = -1;
	     i < PDO_MAX_OBJECTS && data->pdo[i] != 0 &&
	     (data->pdo[i] & PDO_TYPE_MASK) != PDO_TYPE_VARIABLE;
	     i++) {
		volt_i_min = PDO_VAR_MIN_VOLTAGE(data->pdo[i]);
		volt_i_max = PDO_VAR_MAX_VOLTAGE(data->pdo[i]);
		/* Each voltage range should be only once */
		if (volt_i_min == volt_min && volt_i_max == volt_max) {
			return TCPCI_SRC_EMUL_VAR_VOLT_REPEATED;
		}
		/*
		 * Lower minimal voltage should be first, than lower maximal
		 * voltage.
		 */
		if (volt_i_min < volt_min ||
		    (volt_i_min == volt_min && volt_i_max < volt_max)) {
			return TCPCI_SRC_EMUL_VAR_VOLT_NOT_IN_ORDER;
		}
		/* Save current voltage */
		volt_min = volt_i_min;
		volt_max = volt_i_max;
	}

	/* Check that all PDOs after first 0 are unused and set to 0 */
	for (; i < PDO_MAX_OBJECTS; i++) {
		if (data->pdo[i] != 0) {
			return TCPCI_SRC_EMUL_PDO_AFTER_ZERO;
		}
	}

	return TCPCI_SRC_EMUL_CHECK_PDO_OK;
}

/** Check description in emul_tcpci_partner_src.h */
void tcpci_src_emul_init_data(struct tcpci_src_emul_data *data)
{
	/* By default there is only PDO 5v@3A */
	data->pdo[0] = PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED);
	for (int i = 1; i < PDO_MAX_OBJECTS; i++) {
		data->pdo[i] = 0;
	}
}

/** Check description in emul_tcpci_parnter_src.h */
void tcpci_src_emul_init(struct tcpci_src_emul *emul)
{
	tcpci_partner_init(&emul->common_data);

	emul->common_data.data_role = PD_ROLE_UFP;
	emul->common_data.power_role = PD_ROLE_SOURCE;
	emul->common_data.rev = PD_REV20;

	emul->ops.transmit = tcpci_src_emul_transmit_op;
	emul->ops.rx_consumed = tcpci_src_emul_rx_consumed_op;
	emul->ops.control_change = NULL;

	tcpci_src_emul_init_data(&emul->data);
}
