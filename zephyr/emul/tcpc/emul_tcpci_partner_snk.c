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
 * @param delay Optional delay
 *
 * @return 0 on success
 * @return -ENOMEM when there is no free memory for message
 * @return -EINVAL on TCPCI emulator add RX message error
 */
static int tcpci_snk_emul_send_capability_msg(struct tcpci_snk_emul_data *data,
					      uint64_t delay)
{
	int pdos;

	/* Find number of PDOs */
	pdos = tcpci_snk_emul_num_of_pdos(data);

	return tcpci_partner_send_data_msg(&data->common_data,
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
 * @param msg Source capability message
 */
static void tcpci_snk_emul_handle_source_cap(struct tcpci_snk_emul_data *data,
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

	tcpci_partner_send_data_msg(&data->common_data, PD_DATA_REQUEST, &rdo,
				    1 /* = data_obj_num */, 0 /* = delay */);
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
	struct tcpci_snk_emul_data *data =
		CONTAINER_OF(ops, struct tcpci_snk_emul_data, ops);
	uint16_t header;

	/* Acknowledge that message was sent successfully */
	tcpci_emul_partner_msg_status(emul, TCPCI_EMUL_TX_SUCCESS);

	/* Handle hard reset */
	if (type == TCPCI_MSG_TX_HARD_RESET) {
		tcpci_partner_clear_msg_queue(&data->common_data);
		data->common_data.msg_id = 0;
		return;
	}

	/* Handle only SOP messages */
	if (type != TCPCI_MSG_SOP) {
		return;
	}

	LOG_HEXDUMP_INF(tx_msg->buf, tx_msg->cnt,
			"USB-C sink received message");

	header = sys_get_le16(tx_msg->buf);

	if (PD_HEADER_CNT(header)) {
		/* Handle data message */
		switch (PD_HEADER_TYPE(header)) {
		case PD_DATA_SOURCE_CAP:
			tcpci_snk_emul_handle_source_cap(data, tx_msg);
			break;
		case PD_DATA_VENDOR_DEF:
			/* VDM (vendor defined message) - ignore */
			break;
		default:
			tcpci_partner_send_control_msg(&data->common_data,
						       PD_CTRL_REJECT, 0);
			break;
		}
	} else {
		/* Handle control message */
		switch (PD_HEADER_TYPE(header)) {
		case PD_CTRL_GET_SOURCE_CAP:
			tcpci_partner_send_control_msg(&data->common_data,
						       PD_CTRL_REJECT, 0);
			break;
		case PD_CTRL_GET_SINK_CAP:
			tcpci_snk_emul_send_capability_msg(data, 0);
			break;
		case PD_CTRL_DR_SWAP:
			tcpci_partner_send_control_msg(&data->common_data,
						       PD_CTRL_REJECT, 0);
			break;
		case PD_CTRL_SOFT_RESET:
			data->common_data.msg_id = 0;
			tcpci_partner_send_control_msg(&data->common_data,
						       PD_CTRL_ACCEPT, 0);
			break;
		case PD_CTRL_ACCEPT:
			break;
		case PD_CTRL_REJECT:
			break;
		case PD_CTRL_PING:
			break;
		case PD_CTRL_PS_RDY:
			break;
		default:
			tcpci_partner_send_control_msg(&data->common_data,
						       PD_CTRL_REJECT, 0);
			break;
		}
	}
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
				    const struct emul *tcpci_emul)
{
	int ret;

	tcpci_emul_set_partner_ops(tcpci_emul, &data->ops);
	ret = tcpci_emul_connect_partner(tcpci_emul, PD_ROLE_SINK,
					 TYPEC_CC_VOLT_RD,
					 TYPEC_CC_VOLT_OPEN, POLARITY_CC1);
	if (!ret) {
		data->common_data.tcpci_emul = tcpci_emul;
	}

	return ret;
}

/** Check description in emul_tcpci_snk.h */
void tcpci_snk_emul_init(struct tcpci_snk_emul_data *data)
{
	tcpci_partner_init(&data->common_data);

	data->common_data.data_role = PD_ROLE_DFP;
	data->common_data.power_role = PD_ROLE_SINK;
	data->common_data.rev = PD_REV20;

	data->ops.transmit = tcpci_snk_emul_transmit_op;
	data->ops.rx_consumed = tcpci_snk_emul_rx_consumed_op;
	data->ops.control_change = NULL;

	/* By default there is only PDO 5v@500mA */
	data->pdo[0] = PDO_FIXED(5000, 500, 0);
	for (int i = 1; i < PDO_MAX_OBJECTS; i++) {
		data->pdo[i] = 0;
	}
}
