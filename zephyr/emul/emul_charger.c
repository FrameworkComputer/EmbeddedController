/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(charger_emul, CONFIG_TCPCI_EMUL_LOG_LEVEL);

#include <zephyr.h>

#include "common.h"
#include "emul/emul_charger.h"
#include "emul/emul_tcpci.h"
#include "usb_pd.h"

/** Structure of message used by USB-C charger emulator */
struct charger_emul_msg {
	/** Reserved for k_fifo_* usage */
	void *fifo_reserved;
	/** TCPCI emulator message */
	struct tcpci_emul_msg msg;
	/** Time when message should be sent if message is delayed */
	uint64_t time;
};

/**
 * @brief Allocate message
 *
 * @param size Size of message buffer
 *
 * @return Pointer to new message on success
 * @return NULL on error
 */
static struct charger_emul_msg *charger_emul_alloc_msg(size_t size)
{
	struct charger_emul_msg *new_msg;

	new_msg = k_malloc(sizeof(struct charger_emul_msg));
	if (new_msg == NULL) {
		return NULL;
	}

	new_msg->msg.buf = k_malloc(size);
	if (new_msg->msg.buf == NULL) {
		k_free(new_msg);
		return NULL;
	}

	/* TCPCI message size count include type byte */
	new_msg->msg.cnt = size + 1;

	return new_msg;
}

/**
 * @brief Free message's memory
 *
 * @param msg Pointer to message
 */
static void charger_emul_free_msg(struct charger_emul_msg *msg)
{
	k_free(msg->msg.buf);
	k_free(msg);
}

/**
 * @brief Set header of the message
 *
 * @param data Pointer to USB-C charger emulator
 * @param msg Pointer to message
 * @param type Type of message
 * @param cnt Number of data objects
 */
static void charger_emul_set_header(struct charger_emul_data *data,
				    struct charger_emul_msg *msg,
				    int type, int cnt)
{
	/* Header msg id has only 3 bits and wraps around after 8 messages */
	uint16_t msg_id = data->msg_id & 0x7;
	uint16_t header = PD_HEADER(type, PD_ROLE_SOURCE, PD_ROLE_UFP, msg_id,
				    cnt, PD_REV20, 0 /* ext */);
	data->msg_id++;

	msg->msg.buf[1] = (header >> 8) & 0xff;
	msg->msg.buf[0] = header & 0xff;
}

/**
 * @brief Work function which sends delayed messages
 *
 * @param work Pointer to work structure
 */
static void charger_emul_delayed_send(struct k_work *work)
{
	struct k_work_delayable *kwd = k_work_delayable_from_work(work);
	struct charger_emul_data *data = CONTAINER_OF(kwd,
						      struct charger_emul_data,
						      delayed_send);
	struct charger_emul_msg *msg;
	uint64_t now;
	int ec;

	while (!k_fifo_is_empty(&data->to_send)) {
		/*
		 * It is safe to not check msg == NULL, because this thread is
		 * the only one consumer
		 */
		msg = k_fifo_peek_head(&data->to_send);

		now = k_uptime_get();
		if (now >= msg->time) {
			k_fifo_get(&data->to_send, K_FOREVER);
			ec = tcpci_emul_add_rx_msg(data->tcpci_emul, &msg->msg,
						   true /* send alert */);
			if (ec) {
				charger_emul_free_msg(msg);
			}
		} else {
			k_work_reschedule(kwd, K_MSEC(msg->time - now));
			break;
		}
	}
}

/**
 * @brief Send message to TCPCI emulator or schedule message
 *
 * @param data Pointer to USB-C charger emulator
 * @param msg Pointer to message to send
 * @param delay Optional delay
 *
 * @return 0 on success
 * @return -EINVAL on TCPCI emulator add RX message error
 */
static int charger_emul_send_msg(struct charger_emul_data *data,
				 struct charger_emul_msg *msg, uint64_t delay)
{
	uint64_t now;
	int ec;

	if (delay == 0) {
		ec = tcpci_emul_add_rx_msg(data->tcpci_emul, &msg->msg, true);
		if (ec) {
			charger_emul_free_msg(msg);
		}

		return ec;
	}

	now = k_uptime_get();
	msg->time = now + delay;
	k_fifo_put(&data->to_send, msg);
	/*
	 * This will change execution time of delayed_send only if it is not
	 * already scheduled
	 */
	k_work_schedule(&data->delayed_send, K_MSEC(delay));

	return 0;
}

/**
 * @brief Send capability message constructed from charger emulator PDOs
 *
 * @param data Pointer to USB-C charger emulator
 * @param delay Optional delay
 *
 * @return 0 on success
 * @return -ENOMEM when there is no free memory for message
 * @return -EINVAL on TCPCI emulator add RX message error
 */
static int charger_emul_send_capability_msg(struct charger_emul_data *data,
					    uint64_t delay)
{
	struct charger_emul_msg *msg;
	int pdos;
	int byte;
	int addr;

	/* Find number of PDOs */
	for (pdos = 0; pdos < EMUL_CHARGER_MAX_PDOS; pdos++) {
		if (data->pdo[pdos] == 0) {
			break;
		}
	}

	/* Allocate space for header and 4 bytes for each PDO */
	msg = charger_emul_alloc_msg(2 + pdos * 4);
	if (msg == NULL) {
		return -ENOMEM;
	}

	charger_emul_set_header(data, msg, PD_DATA_SOURCE_CAP, pdos);

	for (int i = 0; i < pdos; i++) {
		/* Address of given PDO in message buffer */
		addr = 2 + i * 4;
		for (byte = 0; byte < 4; byte++) {
			msg->msg.buf[addr + byte] =
				(data->pdo[i] >> (8 * byte)) & 0xff;
		}
	}

	/* Fill tcpci message structure */
	msg->msg.type = TCPCI_MSG_SOP;

	return charger_emul_send_msg(data, msg, delay);
}

/**
 * @brief Send control message with optional delay
 *
 * @param data Pointer to USB-C charger emulator
 * @param type Type of message
 * @param delay Optional delay
 *
 * @return 0 on success
 * @return -ENOMEM when there is no free memory for message
 * @return -EINVAL on TCPCI emulator add RX message error
 */
static int charger_emul_send_control_msg(struct charger_emul_data *data,
					 enum pd_ctrl_msg_type type,
					 uint64_t delay)
{
	struct charger_emul_msg *msg;

	msg = charger_emul_alloc_msg(2);
	if (msg == NULL) {
		return -ENOMEM;
	}

	charger_emul_set_header(data, msg, type, 0);

	/* Fill tcpci message structure */
	msg->msg.type = TCPCI_MSG_SOP;

	return charger_emul_send_msg(data, msg, delay);
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
static void charger_emul_transmit_op(const struct emul *emul,
				     const struct tcpci_emul_partner_ops *ops,
				     const struct tcpci_emul_msg *tx_msg,
				     enum tcpci_msg_type type,
				     int retry)
{
	struct charger_emul_data *data = CONTAINER_OF(ops,
						      struct charger_emul_data,
						      ops);
	uint16_t header;

	/* Acknowledge that message was sent successfully */
	tcpci_emul_partner_msg_status(emul, TCPCI_EMUL_TX_SUCCESS);

	/* Handle only SOP messages */
	if (type != TCPCI_MSG_SOP) {
		return;
	}

	LOG_HEXDUMP_DBG(tx_msg->buf, tx_msg->cnt, "Charger received message");

	header = (tx_msg->buf[1] << 8) | tx_msg->buf[0];

	if (PD_HEADER_CNT(header)) {
		/* Handle data message */
		switch (PD_HEADER_TYPE(header)) {
		case PD_DATA_REQUEST:
			charger_emul_send_control_msg(data, PD_CTRL_ACCEPT, 0);
			/* PS ready after 15 ms */
			charger_emul_send_control_msg(data, PD_CTRL_PS_RDY, 15);
			break;
		case PD_DATA_VENDOR_DEF:
			/* VDM (vendor defined message) - ignore */
			break;
		default:
			charger_emul_send_control_msg(data, PD_CTRL_REJECT, 0);
			break;
		}
	} else {
		/* Handle control message */
		switch (PD_HEADER_TYPE(header)) {
		case PD_CTRL_GET_SOURCE_CAP:
			charger_emul_send_capability_msg(data, 0);
			break;
		case PD_CTRL_GET_SINK_CAP:
			charger_emul_send_control_msg(data, PD_CTRL_REJECT, 0);
			break;
		case PD_CTRL_DR_SWAP:
			charger_emul_send_control_msg(data, PD_CTRL_REJECT, 0);
			break;
		case PD_CTRL_SOFT_RESET:
			data->msg_id = 0;
			charger_emul_send_control_msg(data, PD_CTRL_ACCEPT, 0);
			/* Send capability after 15 ms to establish PD again */
			charger_emul_send_capability_msg(data, 15);
			break;
		default:
			charger_emul_send_control_msg(data, PD_CTRL_REJECT, 0);
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
static void charger_emul_rx_consumed_op(
		const struct emul *emul,
		const struct tcpci_emul_partner_ops *ops,
		const struct tcpci_emul_msg *rx_msg)
{
	struct charger_emul_msg *msg = CONTAINER_OF(rx_msg,
						    struct charger_emul_msg,
						    msg);

	charger_emul_free_msg(msg);
}

/** Check description in emul_charger.h */
int charger_emul_connect_to_tcpci(struct charger_emul_data *data,
				  const struct emul *tcpci_emul)
{
	int ec;

	tcpci_emul_set_partner_ops(tcpci_emul, &data->ops);
	ec = tcpci_emul_connect_partner(tcpci_emul, PD_ROLE_SOURCE,
					TYPEC_CC_VOLT_RP_3_0,
					TYPEC_CC_VOLT_OPEN, POLARITY_CC1);
	if (ec) {
		return ec;
	}

	data->tcpci_emul = tcpci_emul;

	return charger_emul_send_capability_msg(data, 0);
}

#define PDO_FIXED_FLAGS_MASK						\
	(PDO_FIXED_DUAL_ROLE | PDO_FIXED_UNCONSTRAINED |		\
	 PDO_FIXED_COMM_CAP | PDO_FIXED_DATA_SWAP)

/** Check description in emul_charger.h */
enum check_pdos_res charger_emul_check_pdos(struct charger_emul_data *data)
{
	int volt_i_min;
	int volt_i_max;
	int volt_min;
	int volt_max;
	int i;

	/* Check that first PDO is fixed 5V */
	if ((data->pdo[0] & PDO_TYPE_MASK) != PDO_TYPE_FIXED ||
	    PDO_FIXED_VOLTAGE(data->pdo[0]) != 5000) {
		return CHARGER_EMUL_FIRST_PDO_NO_FIXED_5V;
	}

	/* Check fixed PDOs are before other types and are in correct order */
	for (i = 1, volt_min = -1;
	     i < EMUL_CHARGER_MAX_PDOS && data->pdo[i] != 0 &&
	     (data->pdo[i] & PDO_TYPE_MASK) != PDO_TYPE_FIXED;
	     i++) {
		volt_i_min = PDO_FIXED_VOLTAGE(data->pdo[i]);
		/* Each voltage should be only once */
		if (volt_i_min == volt_min || volt_i_min == 5000) {
			return CHARGER_EMUL_FIXED_VOLT_REPEATED;
		}
		/* Check that voltage is increasing in next PDO */
		if (volt_i_min < volt_min) {
			return CHARGER_EMUL_FIXED_VOLT_NOT_IN_ORDER;
		}
		/* Check that fixed PDOs (except first) have cleared flags */
		if (data->pdo[i] & PDO_FIXED_FLAGS_MASK) {
			return CHARGER_EMUL_NON_FIRST_PDO_FIXED_FLAGS;
		}
		/* Save current voltage */
		volt_min = volt_i_min;
	}

	/* Check battery PDOs are before variable type and are in order */
	for (volt_min = -1, volt_max = -1;
	     i < EMUL_CHARGER_MAX_PDOS && data->pdo[i] != 0 &&
	     (data->pdo[i] & PDO_TYPE_MASK) != PDO_TYPE_BATTERY;
	     i++) {
		volt_i_min = PDO_BATT_MIN_VOLTAGE(data->pdo[i]);
		volt_i_max = PDO_BATT_MAX_VOLTAGE(data->pdo[i]);
		/* Each voltage range should be only once */
		if (volt_i_min == volt_min && volt_i_max == volt_max) {
			return CHARGER_EMUL_BATT_VOLT_REPEATED;
		}
		/*
		 * Lower minimal voltage should be first, than lower maximal
		 * voltage.
		 */
		if (volt_i_min < volt_min ||
		    (volt_i_min == volt_min && volt_i_max < volt_max)) {
			return CHARGER_EMUL_BATT_VOLT_NOT_IN_ORDER;
		}
		/* Save current voltage */
		volt_min = volt_i_min;
		volt_max = volt_i_max;
	}

	/* Check variable PDOs are last and are in correct order */
	for (volt_min = -1, volt_max = -1;
	     i < EMUL_CHARGER_MAX_PDOS && data->pdo[i] != 0 &&
	     (data->pdo[i] & PDO_TYPE_MASK) != PDO_TYPE_VARIABLE;
	     i++) {
		volt_i_min = PDO_VAR_MIN_VOLTAGE(data->pdo[i]);
		volt_i_max = PDO_VAR_MAX_VOLTAGE(data->pdo[i]);
		/* Each voltage range should be only once */
		if (volt_i_min == volt_min && volt_i_max == volt_max) {
			return CHARGER_EMUL_VAR_VOLT_REPEATED;
		}
		/*
		 * Lower minimal voltage should be first, than lower maximal
		 * voltage.
		 */
		if (volt_i_min < volt_min ||
		    (volt_i_min == volt_min && volt_i_max < volt_max)) {
			return CHARGER_EMUL_VAR_VOLT_NOT_IN_ORDER;
		}
		/* Save current voltage */
		volt_min = volt_i_min;
		volt_max = volt_i_max;
	}

	/* Check that all PDOs after first 0 are unused and set to 0 */
	for (; i < EMUL_CHARGER_MAX_PDOS; i++) {
		if (data->pdo[i] != 0) {
			return CHARGER_EMUL_PDO_AFTER_ZERO;
		}
	}

	return CHARGER_EMUL_CHECK_PDO_OK;
}

/** Check description in emul_charger.h */
void charger_emul_init(struct charger_emul_data *data)
{
	k_work_init_delayable(&data->delayed_send, charger_emul_delayed_send);
	k_fifo_init(&data->to_send);

	data->ops.transmit = charger_emul_transmit_op;
	data->ops.rx_consumed = charger_emul_rx_consumed_op;
	data->ops.control_change = NULL;

	/* By default there is only PDO 5v@3A */
	data->pdo[0] = PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED);
	for (int i = 1; i < EMUL_CHARGER_MAX_PDOS; i++) {
		data->pdo[i] = 0;
	}
}
