/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(tcpci_snk_emul, CONFIG_TCPCI_EMUL_LOG_LEVEL);

#include <sys/byteorder.h>
#include <zephyr.h>

#include "common.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "usb_pd.h"

/** Length of PDO, RDO and BIST request object in SOP message in bytes */
#define TCPCI_MSG_DO_LEN	4
/** Length of header in SOP message in bytes  */
#define TCPCI_MSG_HEADER_LEN	2

/**
 * @brief Get number of PDOs that will be present in sink capability message
 *
 * @param data Pointer to USB-C sink emulator
 *
 * @return Number of PDOs that will be present in sink capability message
 */
static int tcpci_snk_emul_num_of_pdos(struct tcpci_snk_emul_data *data)
{
	for (int pdos = 0; pdos < PDO_MAX_OBJECTS; pdos++) {
		if (data->pdo[pdos] == 0) {
			return pdos;
		}
	}

	return PDO_MAX_OBJECTS;
}

/**
 * @brief Send capability message constructed from USB-C sink emulator PDOs
 *
 * @param data Pointer to USB-C sink emulator
 * @param common_data Pointer to common TCPCI partner data
 * @param delay Optional delay
 *
 * @return 0 on success
 * @return -ENOMEM when there is no free memory for message
 * @return -EINVAL on TCPCI emulator add RX message error
 */
static int tcpci_snk_emul_send_capability_msg(
	struct tcpci_snk_emul_data *data,
	struct tcpci_partner_data *common_data,
	uint64_t delay)
{
	int pdos;

	/* Find number of PDOs */
	pdos = tcpci_snk_emul_num_of_pdos(data);

	return tcpci_partner_send_data_msg(common_data,
					   PD_DATA_SINK_CAP,
					   data->pdo, pdos, delay);
}

/**
 * @brief Check if given source PDO satisfy given sink PDO
 *
 * @param src_pdo PDO presented in source capabilities
 * @param snk_pdo PDO presented in sink capabilities
 *
 * @return 0 on success
 * @return -1 if PDOs are different types, PDOs type is unknown or source
 *         voltage not satisfy sink
 * @return Positive value when voltage is OK, but source cannot provide enough
 *         current for sink. Amount of missing current is returned value in
 *         10mA units.
 */
static int tcpci_snk_emul_are_pdos_complementary(uint32_t src_pdo,
						 uint32_t snk_pdo)
{
	uint32_t pdo_type = src_pdo & PDO_TYPE_MASK;
	int missing_current;

	if ((snk_pdo & PDO_TYPE_MASK) != pdo_type) {
		return -1;
	}

	switch (pdo_type) {
	case PDO_TYPE_FIXED:
		if (PDO_FIXED_VOLTAGE(snk_pdo) != PDO_FIXED_VOLTAGE(src_pdo)) {
			/* Voltage doesn't match */
			return -1;
		}
		missing_current = PDO_FIXED_CURRENT(snk_pdo) -
				  PDO_FIXED_CURRENT(src_pdo);
		break;
	case PDO_TYPE_BATTERY:
		if ((PDO_BATT_MIN_VOLTAGE(snk_pdo) <
		     PDO_BATT_MIN_VOLTAGE(src_pdo)) ||
		    (PDO_BATT_MAX_VOLTAGE(snk_pdo) >
		     PDO_BATT_MAX_VOLTAGE(src_pdo))) {
			/* Voltage not in range */
			return -1;
		}
		/*
		 * Convert to current I * 10[mA] = P * 250[mW] / V * 50[mV]
		 * = P / V * 5 [A] = P / V * 500 * 10[mA]
		 */
		missing_current = (PDO_BATT_MAX_POWER(snk_pdo) -
				   PDO_BATT_MAX_POWER(src_pdo)) * 500 /
				  PDO_BATT_MAX_VOLTAGE(src_pdo);
		break;
	case PDO_TYPE_VARIABLE:
		if ((PDO_VAR_MIN_VOLTAGE(snk_pdo) <
		     PDO_VAR_MIN_VOLTAGE(src_pdo)) ||
		    (PDO_VAR_MAX_VOLTAGE(snk_pdo) >
		     PDO_VAR_MAX_VOLTAGE(src_pdo))) {
			/* Voltage not in range */
			return -1;
		}
		missing_current = PDO_VAR_MAX_CURRENT(snk_pdo) -
				  PDO_VAR_MAX_CURRENT(src_pdo);
		break;
	default:
		/* Unknown PDO type */
		return -1;
	}

	if (missing_current > 0) {
		/* Voltage is correct, but src doesn't offer enough current */
		return missing_current;
	}

	return 0;
}

/**
 * @brief Get given PDO from source capability message
 *
 * @param msg Source capability message
 * @param pdo_num Number of PDO to get. First PDO is 0.
 *
 * @return PDO on success
 * @return 0 when there is no PDO of given index in message
 */
static uint32_t tcpci_snk_emul_get_pdo_from_cap(
			const struct tcpci_emul_msg *msg, int pdo_num)
{
	int addr;

	/* Get address of PDO in message */
	addr = TCPCI_MSG_HEADER_LEN + pdo_num * TCPCI_MSG_DO_LEN;

	if (addr >= msg->cnt) {
		return 0;
	}

	return sys_get_le32(msg->buf + addr);
}

/**
 * @brief Create RDO for given sink and source PDOs
 *
 * @param src_pdo Selected source PDO
 * @param snk_pdo Matching sink PDO
 * @param src_pdo_num Index of source PDO in capability message. First PDO is 1.
 *
 * @return RDO on success
 * @return 0 When type of PDOs doesn't match
 */
static uint32_t tcpci_snk_emul_create_rdo(uint32_t src_pdo, uint32_t snk_pdo,
					  int src_pdo_num)
{
	uint32_t pdo_type = src_pdo & PDO_TYPE_MASK;
	int flags;
	int pow;
	int cur;

	if ((snk_pdo & PDO_TYPE_MASK) != pdo_type) {
		return 0;
	}

	switch (pdo_type) {
	case PDO_TYPE_FIXED:
		if (PDO_FIXED_CURRENT(snk_pdo) > PDO_FIXED_CURRENT(src_pdo)) {
			flags = RDO_CAP_MISMATCH;
			cur = PDO_FIXED_CURRENT(src_pdo);
		} else {
			flags = 0;
			cur = PDO_FIXED_CURRENT(snk_pdo);
		}

		/*
		 * Force mismatch flag if higher capability bit is set. Flags
		 * should be set only in the first PDO (vSafe5V). This statment
		 * will only be true for sink which requries higher voltage than
		 * 5V and doesn't found it in source capabilities.
		 */
		if (snk_pdo & PDO_FIXED_SNK_HIGHER_CAP) {
			flags = RDO_CAP_MISMATCH;
		}

		return RDO_FIXED(src_pdo_num, cur, PDO_FIXED_CURRENT(snk_pdo),
				 flags);
	case PDO_TYPE_BATTERY:
		if (PDO_BATT_MAX_POWER(snk_pdo) > PDO_BATT_MAX_POWER(src_pdo)) {
			flags = RDO_CAP_MISMATCH;
			pow = PDO_BATT_MAX_POWER(src_pdo);
		} else {
			flags = 0;
			pow = PDO_BATT_MAX_POWER(snk_pdo);
		}

		return RDO_BATT(src_pdo_num, pow, PDO_BATT_MAX_POWER(snk_pdo),
				flags);
	case PDO_TYPE_VARIABLE:
		if (PDO_VAR_MAX_CURRENT(snk_pdo) >
		    PDO_VAR_MAX_CURRENT(src_pdo)) {
			flags = RDO_CAP_MISMATCH;
			cur = PDO_VAR_MAX_CURRENT(src_pdo);
		} else {
			flags = 0;
			cur = PDO_VAR_MAX_CURRENT(snk_pdo);
		}
		return RDO_FIXED(src_pdo_num, cur, PDO_VAR_MAX_CURRENT(snk_pdo),
				 flags);
	}

	return 0;
}

/**
 * @brief Respond to source capability message
 *
 * @param data Pointer to USB-C sink emulator
 * @param common_data Pointer to common TCPCI partner data
 * @param msg Source capability message
 */
static void tcpci_snk_emul_handle_source_cap(
	struct tcpci_snk_emul_data *data,
	struct tcpci_partner_data *common_data,
	const struct tcpci_emul_msg *msg)
{
	uint32_t rdo = 0;
	uint32_t pdo;
	int missing_current;
	int skip_first_pdo;
	int snk_pdos;
	int src_pdos;

	/* If higher capability bit is set, skip matching to first (5V) PDO */
	if (data->pdo[0] & PDO_FIXED_SNK_HIGHER_CAP) {
		skip_first_pdo = 1;
	} else {
		skip_first_pdo = 0;
	}

	/* Find number of PDOs */
	snk_pdos = tcpci_snk_emul_num_of_pdos(data);
	src_pdos = (msg->cnt - TCPCI_MSG_HEADER_LEN) / TCPCI_MSG_DO_LEN;

	/* Find if any source PDO satisfy any sink PDO */
	for (int pdo_num = 0; pdo_num < src_pdos; pdo_num++) {
		pdo = tcpci_snk_emul_get_pdo_from_cap(msg, pdo_num);

		for (int i = skip_first_pdo; i < snk_pdos; i++) {
			missing_current = tcpci_snk_emul_are_pdos_complementary(
						pdo, data->pdo[i]);
			if (missing_current == 0) {
				rdo = tcpci_snk_emul_create_rdo(pdo,
								data->pdo[i],
								pdo_num + 1);
				break;
			}
		}

		/* Correct PDO already found */
		if (rdo != 0) {
			break;
		}
	}

	if (rdo == 0) {
		/* Correct PDO wasn't found, let's use 5V */
		pdo = tcpci_snk_emul_get_pdo_from_cap(msg, 0);
		rdo = tcpci_snk_emul_create_rdo(pdo, data->pdo[0], 1);
	}

	/* Expect response for request */
	common_data->wait_for_response = true;
	tcpci_partner_send_data_msg(common_data, PD_DATA_REQUEST, &rdo,
				    1 /* = data_obj_num */, 0 /* = delay */);
}

/** Check description in emul_tcpci_snk.h */
enum tcpci_partner_handler_res tcpci_snk_emul_handle_sop_msg(
	struct tcpci_snk_emul_data *data,
	struct tcpci_partner_data *common_data,
	const struct tcpci_emul_msg *msg)
{
	uint16_t header;

	header = sys_get_le16(msg->buf);

	if (PD_HEADER_CNT(header)) {
		/* Handle data message */
		switch (PD_HEADER_TYPE(header)) {
		case PD_DATA_SOURCE_CAP:
			tcpci_snk_emul_handle_source_cap(data, common_data,
							 msg);
			return TCPCI_PARTNER_COMMON_MSG_HANDLED;
		default:
			return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
		}
	} else {
		/* Handle control message */
		switch (PD_HEADER_TYPE(header)) {
		case PD_CTRL_GET_SINK_CAP:
			tcpci_snk_emul_send_capability_msg(data, common_data,
							   0);
			return TCPCI_PARTNER_COMMON_MSG_HANDLED;
		case PD_CTRL_PING:
			return TCPCI_PARTNER_COMMON_MSG_HANDLED;
		case PD_CTRL_PS_RDY:
			__ASSERT(data->wait_for_ps_rdy,
				 "Unexpected PS RDY message");
			data->wait_for_ps_rdy = false;
			data->pd_completed = true;
			return TCPCI_PARTNER_COMMON_MSG_HANDLED;
		case PD_CTRL_REJECT:
			/* Request rejected. Ask for capabilities again. */
			tcpci_partner_send_control_msg(common_data,
						       PD_CTRL_GET_SOURCE_CAP,
						       0);
			common_data->wait_for_response = false;
			return TCPCI_PARTNER_COMMON_MSG_HANDLED;
		case PD_CTRL_ACCEPT:
			common_data->wait_for_response = false;
			data->wait_for_ps_rdy = true;
			return TCPCI_PARTNER_COMMON_MSG_HANDLED;
		default:
			return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
		}
	}

	return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
}

/** Check description in emul_tcpci_partner_snk.h */
void tcpci_snk_emul_hard_reset(void *data)
{
	struct tcpci_snk_emul *snk_emul = data;

	snk_emul->data.wait_for_ps_rdy = false;
	snk_emul->data.pd_completed = false;
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
static void tcpci_snk_emul_transmit_op(const struct emul *emul,
				       const struct tcpci_emul_partner_ops *ops,
				       const struct tcpci_emul_msg *tx_msg,
				       enum tcpci_msg_type type,
				       int retry)
{
	struct tcpci_snk_emul *snk_emul =
		CONTAINER_OF(ops, struct tcpci_snk_emul, ops);
	enum tcpci_partner_handler_res processed;
	int ret;

	ret = k_mutex_lock(&snk_emul->common_data.transmit_mutex, K_FOREVER);
	if (ret) {
		LOG_ERR("Failed to get SNK mutex");
		/* Inform TCPM that message send failed */
		tcpci_partner_common_msg_handler(&snk_emul->common_data,
						 tx_msg, type,
						 TCPCI_EMUL_TX_FAILED);
		return;
	}

	/* Call common handler */
	processed = tcpci_partner_common_msg_handler(&snk_emul->common_data,
						     tx_msg, type,
						     TCPCI_EMUL_TX_SUCCESS);
	switch (processed) {
	case TCPCI_PARTNER_COMMON_MSG_HARD_RESET:
	case TCPCI_PARTNER_COMMON_MSG_HANDLED:
		/* Message handled nothing to do */
		k_mutex_unlock(&snk_emul->common_data.transmit_mutex);
		return;
	case TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED:
	default:
		/* Continue */
		break;
	}

	/* Handle only SOP messages */
	if (type != TCPCI_MSG_SOP) {
		k_mutex_unlock(&snk_emul->common_data.transmit_mutex);
		return;
	}

	/* Call sink specific handler */
	processed = tcpci_snk_emul_handle_sop_msg(&snk_emul->data,
						  &snk_emul->common_data,
						  tx_msg);
	if (processed == TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED) {
		/* Send reject for not handled messages (PD rev 2.0) */
		tcpci_partner_send_control_msg(&snk_emul->common_data,
					       PD_CTRL_REJECT, 0);
	}
	k_mutex_unlock(&snk_emul->common_data.transmit_mutex);
}

/**
 * @brief Function called when TCPM consumes message. Free message that is no
 *        longer needed.
 *
 * @param emul Pointer to TCPCI emulator
 * @param ops Pointer to partner operations structure
 * @param rx_msg Message that was consumed by TCPM
 */
static void tcpci_snk_emul_rx_consumed_op(
		const struct emul *emul,
		const struct tcpci_emul_partner_ops *ops,
		const struct tcpci_emul_msg *rx_msg)
{
	struct tcpci_partner_msg *msg = CONTAINER_OF(rx_msg,
						     struct tcpci_partner_msg,
						     msg);

	tcpci_partner_free_msg(msg);
}

/** Check description in emul_tcpci_snk.h */
int tcpci_snk_emul_connect_to_tcpci(struct tcpci_snk_emul_data *data,
				    struct tcpci_partner_data *common_data,
				    const struct tcpci_emul_partner_ops *ops,
				    const struct emul *tcpci_emul)
{
	int ret;

	tcpci_emul_set_partner_ops(tcpci_emul, ops);
	ret = tcpci_emul_connect_partner(tcpci_emul, PD_ROLE_SINK,
					 TYPEC_CC_VOLT_RD,
					 TYPEC_CC_VOLT_OPEN, POLARITY_CC1);
	if (!ret) {
		common_data->tcpci_emul = tcpci_emul;
	}

	data->wait_for_ps_rdy = false;
	data->pd_completed = false;

	return ret;
}

/** Check description in emul_tcpci_snk.h */
void tcpci_snk_emul_init_data(struct tcpci_snk_emul_data *data)
{
	/* By default there is only PDO 5v@500mA */
	data->pdo[0] = PDO_FIXED(5000, 500, 0);
	for (int i = 1; i < PDO_MAX_OBJECTS; i++) {
		data->pdo[i] = 0;
	}

	data->wait_for_ps_rdy = false;
	data->pd_completed = false;

}

/** Check description in emul_tcpci_snk.h */
void tcpci_snk_emul_init(struct tcpci_snk_emul *emul)
{
	tcpci_partner_init(&emul->common_data, tcpci_snk_emul_hard_reset, emul);

	emul->common_data.data_role = PD_ROLE_DFP;
	emul->common_data.power_role = PD_ROLE_SINK;
	emul->common_data.rev = PD_REV20;

	emul->ops.transmit = tcpci_snk_emul_transmit_op;
	emul->ops.rx_consumed = tcpci_snk_emul_rx_consumed_op;
	emul->ops.control_change = NULL;

	tcpci_snk_emul_init_data(&emul->data);
}
