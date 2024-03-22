/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "driver/tcpm/tcpci.h"
#include "ec_commands.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "usb_pd.h"
#include "util.h"

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(tcpci_partner, CONFIG_TCPCI_EMUL_LOG_LEVEL);

/** Length of PDO, RDO and BIST request object in SOP message in bytes */
#define TCPCI_MSG_DO_LEN 4
/** Length of header in SOP message in bytes  */
#define TCPCI_MSG_HEADER_LEN 2
/** Length of extended header in bytes  */
#define TCPCI_MSG_EXT_HEADER_LEN 2

void tcpci_partner_common_hard_reset_as_role(struct tcpci_partner_data *data,
					     enum pd_power_role power_role)
{
	data->power_role = power_role;
	data->data_role = power_role == PD_ROLE_SOURCE ? PD_ROLE_DFP :
							 PD_ROLE_UFP;
	data->vconn_role = power_role == PD_ROLE_SOURCE ? PD_ROLE_VCONN_SRC :
							  PD_ROLE_VCONN_OFF;
	data->displayport_configured = false;
	data->entered_svid = 0;
	atomic_clear(&data->mode_enter_attempts);
}

/**
 * @brief Allocate space for a PD message. Do not call directly; use
 *        tcpci_partner_alloc_standard_msg() or
 * tcpci_partner_alloc_extended_msg() depending on the type of message.
 *
 * @param size Size of the message in bytes, including header(s)
 *
 * @return Pointer to new message on success
 * @return NULL on error
 */
static struct tcpci_partner_msg *tcpci_partner_alloc_msg_helper(size_t size)
{
	struct tcpci_partner_msg *new_msg;

	new_msg = calloc(1, sizeof(struct tcpci_partner_msg));
	if (new_msg == NULL) {
		return NULL;
	}

	new_msg->msg.buf = calloc(1, size);
	if (new_msg->msg.buf == NULL) {
		free(new_msg);
		return NULL;
	}

	/* Set default message type to SOP */
	new_msg->msg.sop_type = TCPCI_MSG_SOP;
	new_msg->msg.cnt = size;

	return new_msg;
}

/**
 * @brief Allocate space for a standard (non-extended) message, containing the
 * specified number of data objects.
 *
 * @param num_data_objects Number of 32-bit DOs this message contains, if data
 * message. Pass 0 if control message.
 * @return struct tcpci_partner_msg* if successful
 * @return NULL in case of error
 */
static struct tcpci_partner_msg *
tcpci_partner_alloc_standard_msg(int num_data_objects)
{
	struct tcpci_partner_msg *msg = tcpci_partner_alloc_msg_helper(
		TCPCI_MSG_HEADER_LEN + TCPCI_MSG_DO_LEN * num_data_objects);

	if (msg) {
		msg->data_objects = num_data_objects;
	}

	return msg;
}

/**
 * @brief Allocate space for an extended message, containing a payload of
 * specified size
 *
 * @param payload_size Size of extended message payload. Do not count either
 * message header.
 * @return struct tcpci_partner_msg* if successful
 * @return NULL in case of error
 */
static struct tcpci_partner_msg *
tcpci_partner_alloc_extended_msg(size_t payload_size)
{
	/* Currently, the emulators only support extended messages that can fit
	 * into a single chunk. Enforce that here.
	 */

	__ASSERT(payload_size <= PD_MAX_EXTENDED_MSG_CHUNK_LEN,
		 "Message must fit into a single chunk");

	struct tcpci_partner_msg *msg = tcpci_partner_alloc_msg_helper(
		TCPCI_MSG_HEADER_LEN + TCPCI_MSG_EXT_HEADER_LEN + payload_size);

	if (msg) {
		msg->extended = true;

		/* Update the number of data objects with the number of 4-byte
		 * words in the payload, rounding up. This includes the 2-byte
		 * Extended Message Header (USB-PD spec Rev 3.0, V1.1,
		 * section 6.2.1.2.1)
		 */

		msg->data_objects = DIV_ROUND_UP(
			payload_size + TCPCI_MSG_EXT_HEADER_LEN, 4);
	}

	return msg;
}

/**
 * @brief Alloc and append message to log if collect_msg_log flag is set
 *
 * @param data Pointer to TCPCI partner emulator
 * @param msg The PD message to log
 * @param sender Who send the message
 * @param status If message was received/send correctly
 *
 * @return Pointer to message status
 */
static enum tcpci_emul_tx_status *tcpci_partner_log_msg(
	struct tcpci_partner_data *data, const struct tcpci_emul_msg *msg,
	enum tcpci_partner_msg_sender sender, enum tcpci_emul_tx_status status)
{
	struct tcpci_partner_log_msg *log_msg;
	int cnt;
	int ret;

	if (!data->collect_msg_log) {
		return NULL;
	}

	log_msg = malloc(sizeof(struct tcpci_partner_log_msg));
	if (log_msg == NULL) {
		return NULL;
	}

	/* We log length of actual buffer without SOP byte */
	cnt = msg->cnt;
	log_msg->buf = malloc(cnt);
	if (log_msg->buf == NULL) {
		free(log_msg);
		return NULL;
	}

	log_msg->cnt = cnt;
	log_msg->sop = msg->sop_type;
	log_msg->time = k_uptime_get();
	log_msg->sender = sender;
	log_msg->status = status;

	memcpy(log_msg->buf, msg->buf, cnt);

	ret = k_mutex_lock(&data->msg_log_mutex, K_FOREVER);
	if (ret) {
		free(log_msg->buf);
		free(log_msg);
		return NULL;
	}

	sys_slist_append(&data->msg_log, &log_msg->node);

	k_mutex_unlock(&data->msg_log_mutex);

	return &log_msg->status;
}

void tcpci_partner_free_msg(struct tcpci_partner_msg *msg)
{
	free(msg->msg.buf);
	free(msg);
}

void tcpci_partner_set_header(struct tcpci_partner_data *data,
			      struct tcpci_partner_msg *msg)
{
	uint16_t msg_id;
	uint16_t header;

	/* Header msg id has only 3 bits and wraps around after 8 messages */
	if (msg->msg.sop_type == TCPCI_MSG_SOP) {
		msg_id = data->sop_msg_id & 0x7;
		header = PD_HEADER(msg->type, data->power_role, data->data_role,
				   msg_id, msg->data_objects, data->rev,
				   msg->extended);
		data->sop_msg_id++;
	} else if (msg->msg.sop_type == TCPCI_MSG_SOP_PRIME) {
		msg_id = data->sop_prime_msg_id & 0x7;
		header = PD_HEADER(msg->type, PD_PLUG_FROM_CABLE, 0, msg_id,
				   msg->data_objects, data->rev, msg->extended);
		data->sop_prime_msg_id++;
	} else {
		return;
	}

	msg->msg.buf[1] = (header >> 8) & 0xff;
	msg->msg.buf[0] = header & 0xff;
}

/**
 * @brief Convert return code from tcpci_emul_add_rx_msg to one of the enum
 *        tcpci_emul_tx_status value
 *
 * @param ret Return code of the tcpci_emul_add_rx_msg function
 *
 * @return One ot the enum tcpci_emul_tx_status value
 */
static enum tcpci_emul_tx_status tcpci_partner_add_rx_msg_to_status(int ret)
{
	switch (ret) {
	case TCPCI_EMUL_TX_SUCCESS:
	case TCPCI_EMUL_TX_FAILED:
		return ret;
	}

	/* Convert all other error codes to unknown value */
	return TCPCI_EMUL_TX_UNKNOWN;
}

/**
 * @brief Work function which sends delayed messages
 *
 * @param work Pointer to work structure
 */
static void tcpci_partner_delayed_send(void *fifo_data)
{
	struct tcpci_partner_data *data =
		CONTAINER_OF(fifo_data, struct tcpci_partner_data, fifo_data);
	enum tcpci_emul_tx_status status;
	struct tcpci_partner_msg *msg;
	uint64_t now;
	int ret;

	do {
		ret = k_mutex_lock(&data->to_send_mutex, K_FOREVER);
	} while (ret);

	while (!sys_slist_is_empty(&data->to_send)) {
		msg = SYS_SLIST_PEEK_HEAD_CONTAINER(&data->to_send, msg, node);

		now = k_uptime_get();
		if (now >= msg->time) {
			sys_slist_get_not_empty(&data->to_send);
			k_mutex_unlock(&data->to_send_mutex);

			tcpci_partner_set_header(data, msg);
			__ASSERT(data->tcpci_emul,
				 "Disconnected partner send message");
			ret = tcpci_emul_add_rx_msg(data->tcpci_emul, &msg->msg,
						    true /* send alert */);
			status = tcpci_partner_add_rx_msg_to_status(ret);
			tcpci_partner_log_msg(data, &msg->msg,
					      TCPCI_PARTNER_SENDER_PARTNER,
					      status);
			if (ret != TCPCI_EMUL_TX_SUCCESS) {
				tcpci_partner_free_msg(msg);
			}

			do {
				ret = k_mutex_lock(&data->to_send_mutex,
						   K_FOREVER);
			} while (ret);
		} else {
			k_timer_start(&data->delayed_send,
				      K_MSEC(msg->time - now), K_NO_WAIT);
			break;
		}
	}

	k_mutex_unlock(&data->to_send_mutex);
}

/** FIFO to schedule TCPCI partners that needs to send message */
K_FIFO_DEFINE(delayed_send_fifo);

/**
 * @brief Thread which sends delayed messages for TCPCI partners
 *
 * @param a unused
 * @param b unused
 * @param c unused
 */
static void tcpci_partner_delayed_send_thread(void *a, void *b, void *c)
{
	void *fifo_data;

	while (1) {
		fifo_data = k_fifo_get(&delayed_send_fifo, K_FOREVER);
		tcpci_partner_delayed_send(fifo_data);
	}
}

/** Thread for sending delayed messages */
K_THREAD_DEFINE(tcpci_partner_delayed_send_tid, 512 /* stack size */,
		tcpci_partner_delayed_send_thread, NULL, NULL, NULL,
		0 /* priority */, 0, 0);

/**
 * @brief Timeout handler which adds TCPCI partner that has pending delayed
 *        message to send
 *
 * @param timer Pointer to timer which triggered timeout
 */
static void tcpci_partner_delayed_send_timer(struct k_timer *timer)
{
	struct tcpci_partner_data *data =
		CONTAINER_OF(timer, struct tcpci_partner_data, delayed_send);

	k_fifo_put(&delayed_send_fifo, &data->fifo_data);
}

int tcpci_partner_send_msg(struct tcpci_partner_data *data,
			   struct tcpci_partner_msg *msg, uint64_t delay)
{
	struct tcpci_partner_msg *next_msg;
	struct tcpci_partner_msg *prev_msg;
	uint64_t now;
	int ret;

	if (delay == 0) {
		__ASSERT(data->tcpci_emul, "Disconnected partner send message");
		tcpci_partner_set_header(data, msg);
		ret = tcpci_emul_add_rx_msg(data->tcpci_emul, &msg->msg, true);
		tcpci_partner_log_msg(data, &msg->msg,
				      TCPCI_PARTNER_SENDER_PARTNER,
				      tcpci_partner_add_rx_msg_to_status(ret));
		if (ret != TCPCI_EMUL_TX_SUCCESS) {
			tcpci_partner_free_msg(msg);
		}

		return ret;
	}

	now = k_uptime_get();
	msg->time = now + delay;

	ret = k_mutex_lock(&data->to_send_mutex, K_FOREVER);
	if (ret) {
		tcpci_partner_free_msg(msg);

		return ret;
	}

	prev_msg =
		SYS_SLIST_PEEK_HEAD_CONTAINER(&data->to_send, prev_msg, node);
	/* Current message should be sent first */
	if (prev_msg == NULL || prev_msg->time > msg->time) {
		sys_slist_prepend(&data->to_send, &msg->node);
		k_timer_start(&data->delayed_send, K_MSEC(delay), K_NO_WAIT);
		k_mutex_unlock(&data->to_send_mutex);
		return 0;
	}

	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&data->to_send, prev_msg, next_msg,
					  node)
	{
		/*
		 * If we reach tail or next message should be sent after new
		 * message, insert new message to the list.
		 */
		if (next_msg == NULL || next_msg->time > msg->time) {
			sys_slist_insert(&data->to_send, &prev_msg->node,
					 &msg->node);
			k_mutex_unlock(&data->to_send_mutex);
			return 0;
		}
	}

	__ASSERT(0, "Message should be always inserted to the list");

	return -1;
}

int tcpci_partner_send_control_msg(struct tcpci_partner_data *data,
				   enum pd_ctrl_msg_type type, uint64_t delay)
{
	struct tcpci_partner_msg *msg;

	msg = tcpci_partner_alloc_standard_msg(0);
	if (msg == NULL) {
		return -ENOMEM;
	}

	msg->type = type;

	switch (msg->type) {
	case PD_CTRL_DR_SWAP:
	case PD_CTRL_VCONN_SWAP:
		/* For supported message types, remember the control request
		 * initiated, so the partner can handle the responses.
		 * (Eventually, all messages that can start an AMS should be
		 * supported.)
		 */
		tcpci_partner_common_set_ams_ctrl_msg(data, msg->type);
		break;
	default:
		/* For messages that do not start an AMS, there is nothing to
		 * record.
		 */
		break;
	}

	return tcpci_partner_send_msg(data, msg, delay);
}

int tcpci_partner_send_data_msg(struct tcpci_partner_data *data,
				enum pd_data_msg_type type, uint32_t *data_obj,
				int data_obj_num, uint64_t delay)
{
	struct tcpci_partner_msg *msg;
	int addr;

	msg = tcpci_partner_alloc_standard_msg(data_obj_num);
	if (msg == NULL) {
		return -ENOMEM;
	}

	for (int i = 0; i < data_obj_num; i++) {
		/* Address of given data object in message buffer */
		addr = TCPCI_MSG_HEADER_LEN + i * TCPCI_MSG_DO_LEN;
		sys_put_le32(data_obj[i], msg->msg.buf + addr);
	}

	msg->type = type;

	return tcpci_partner_send_msg(data, msg, delay);
}

/* Note: Cables can send from both SOP' and SOP'', so accept a type argument */
int tcpci_cable_send_data_msg(struct tcpci_partner_data *data,
			      enum pd_data_msg_type type, uint32_t *data_obj,
			      int data_obj_num, enum tcpci_msg_type sop_type,
			      uint64_t delay)
{
	struct tcpci_partner_msg *msg;
	int addr;

	/* TODO(b/243151272): Add SOP'' support */
	if (sop_type != TCPCI_MSG_SOP_PRIME)
		return -EINVAL;

	msg = tcpci_partner_alloc_standard_msg(data_obj_num);
	if (msg == NULL) {
		return -ENOMEM;
	}

	for (int i = 0; i < data_obj_num; i++) {
		/* Address of given data object in message buffer */
		addr = TCPCI_MSG_HEADER_LEN + i * TCPCI_MSG_DO_LEN;
		sys_put_le32(data_obj[i], msg->msg.buf + addr);
	}

	msg->msg.sop_type = sop_type;
	msg->type = type;

	return tcpci_partner_send_msg(data, msg, delay);
}

int tcpci_partner_clear_msg_queue(struct tcpci_partner_data *data)
{
	struct tcpci_partner_msg *msg;
	int ret;

	k_timer_stop(&data->delayed_send);

	ret = k_mutex_lock(&data->to_send_mutex, K_FOREVER);
	if (ret) {
		return ret;
	}

	while (!sys_slist_is_empty(&data->to_send)) {
		msg = CONTAINER_OF(sys_slist_get_not_empty(&data->to_send),
				   struct tcpci_partner_msg, node);
		tcpci_partner_free_msg(msg);
	}

	k_mutex_unlock(&data->to_send_mutex);

	return 0;
}

/**
 * @brief Reset common data to state after hard reset (reset counters, flags,
 *        clear message queue)
 *
 * @param data Pointer to TCPCI partner emulator
 */
static void tcpci_partner_common_reset(struct tcpci_partner_data *data)
{
	tcpci_partner_clear_msg_queue(data);
	data->sop_msg_id = 0;
	data->sop_prime_msg_id = 0;
	data->sop_recv_msg_id = -1;
	data->sop_prime_recv_msg_id = -1;
	data->in_soft_reset = false;
	tcpci_partner_stop_sender_response_timer(data);
	tcpci_partner_common_clear_ams_ctrl_msg(data);
}

/**
 * @brief Common action on HardReset message send and receive which is calling
 *        hard_reset callback on all extensions and resetting common data
 *
 * @param data Pointer to TCPCI partner emulator
 */
static void tcpci_partner_common_hard_reset(struct tcpci_partner_data *data)
{
	struct tcpci_partner_extension *ext;

	tcpci_partner_common_reset(data);
	for (ext = data->extensions; ext != NULL; ext = ext->next) {
		if (ext->ops->hard_reset) {
			ext->ops->hard_reset(ext, data);
		}
	}
}

void tcpci_partner_common_send_hard_reset(struct tcpci_partner_data *data)
{
	struct tcpci_partner_msg *msg;

	tcpci_partner_common_hard_reset(data);

	msg = tcpci_partner_alloc_standard_msg(0);
	msg->msg.sop_type = TCPCI_MSG_TX_HARD_RESET;

	tcpci_partner_send_msg(data, msg, 0);
}

void tcpci_partner_common_send_soft_reset(struct tcpci_partner_data *data)
{
	/* Reset counters */
	data->sop_msg_id = 0;
	data->sop_prime_msg_id = 0;
	data->sop_recv_msg_id = -1;
	data->sop_prime_recv_msg_id = -1;

	tcpci_partner_common_clear_ams_ctrl_msg(data);

	/* Send message */
	tcpci_partner_send_control_msg(data, PD_CTRL_SOFT_RESET, 0);
	/* Wait for accept of soft reset */
	data->in_soft_reset = true;
	tcpci_partner_start_sender_response_timer(data);
}

int tcpci_partner_send_extended_msg(struct tcpci_partner_data *data,
				    enum pd_ext_msg_type type, uint64_t delay,
				    uint8_t *payload, size_t payload_size)
{
	struct tcpci_partner_msg *msg;

	msg = tcpci_partner_alloc_extended_msg(payload_size);
	if (msg == NULL) {
		return -ENOMEM;
	}

	msg->type = type;

	/* Apply extended message header. We currently do not support
	 * multiple chunks.
	 */

	sys_put_le16(PD_EXT_HEADER(0, 0, payload_size), &msg->msg.buf[2]);

	/* Copy in payload */
	memcpy(&msg->msg.buf[4], payload, payload_size);

	return tcpci_partner_send_msg(data, msg, delay);
}

/** Check description in emul_common_tcpci_partner.h */
void tcpci_partner_common_send_get_battery_capabilities(
	struct tcpci_partner_data *data, int battery_index)
{
	__ASSERT(battery_index >= 0 && battery_index < PD_BATT_MAX,
		 "Battery index out of range");
	__ASSERT(data->battery_capabilities.index < 0,
		 "Get Battery Capabilities request already in progress");

	LOG_INF("Send battery cap request");

	/* Get_Battery_Cap message payload */
	uint8_t payload[1] = { [0] = battery_index };

	/* Keep track which battery we requested capabilities for */
	data->battery_capabilities.index = battery_index;
	int ret = tcpci_partner_send_extended_msg(data, PD_EXT_GET_BATTERY_CAP,
						  0, payload, sizeof(payload));
	if (ret) {
		LOG_ERR("Send battery capacity result: %d", ret);
	}
	tcpci_partner_start_sender_response_timer(data);
}

/**
 * @brief Handler for response timeout
 *
 * @param timer Pointer to timer which triggered timeout
 */
static void tcpci_partner_sender_response_timeout(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct tcpci_partner_data *data = CONTAINER_OF(
		dwork, struct tcpci_partner_data, sender_response_timeout);

	if (k_mutex_lock(&data->transmit_mutex, K_NO_WAIT) != 0) {
		/*
		 * Emulator is probably handling received message,
		 * try later if timer wasn't stopped.
		 */
		k_work_submit(work);
		return;
	}

	/* Make sure that timer isn't stopped */
	if (k_work_busy_get(work) & K_WORK_CANCELING) {
		k_mutex_unlock(&data->transmit_mutex);
		return;
	}

	data->tcpm_timeouts++;
	tcpci_partner_common_send_hard_reset(data);
	LOG_ERR("Timeout for TCPM response");

	k_mutex_unlock(&data->transmit_mutex);
}

void tcpci_partner_start_sender_response_timer(struct tcpci_partner_data *data)
{
	k_work_schedule(&data->sender_response_timeout,
			TCPCI_PARTNER_RESPONSE_TIMEOUT);
	data->wait_for_response = true;
}

void tcpci_partner_stop_sender_response_timer(struct tcpci_partner_data *data)
{
	k_work_cancel_delayable(&data->sender_response_timeout);
	data->wait_for_response = false;
}

static enum tcpci_partner_handler_res
tcpci_partner_common_vdm_handler(struct tcpci_partner_data *data,
				 const struct tcpci_emul_msg *message)
{
	uint32_t vdm_header = sys_get_le32(message->buf + TCPCI_MSG_HEADER_LEN);

	/* TCPCI r2.0: Ignore unsupported VDMs. Don't handle command types other
	 * than REQ or unstructured VDMs.
	 * TODO(b/225397796): Validate VDM fields more thoroughly.
	 */
	if (PD_VDO_CMDT(vdm_header) != CMDT_INIT || !PD_VDO_SVDM(vdm_header)) {
		return TCPCI_PARTNER_COMMON_MSG_HANDLED;
	}

	switch (PD_VDO_CMD(vdm_header)) {
	case CMD_DISCOVER_IDENT:
		if (data->identity_vdos > 0) {
			tcpci_partner_send_data_msg(data, PD_DATA_VENDOR_DEF,
						    data->identity_vdm,
						    data->identity_vdos, 0);
		}
		return TCPCI_PARTNER_COMMON_MSG_HANDLED;
	case CMD_DISCOVER_SVID:
		if (data->svids_vdos > 0) {
			tcpci_partner_send_data_msg(data, PD_DATA_VENDOR_DEF,
						    data->svids_vdm,
						    data->svids_vdos, 0);
		}
		return TCPCI_PARTNER_COMMON_MSG_HANDLED;
	case CMD_DISCOVER_MODES:
		if (data->modes_vdos > 0) {
			tcpci_partner_send_data_msg(data, PD_DATA_VENDOR_DEF,
						    data->modes_vdm,
						    data->modes_vdos, 0);
		}
		return TCPCI_PARTNER_COMMON_MSG_HANDLED;
	case CMD_ENTER_MODE:
		/* Partner emulator only supports entering one mode */
		if (data->enter_mode_vdos > 0 &&
		    (PD_VDO_VID(vdm_header) ==
		     PD_VDO_VID(data->enter_mode_vdm[0]))) {
			/* Squirrel away the SVID if we're sending ACK */
			if (PD_VDO_CMDT(data->enter_mode_vdm[0]) ==
			    CMDT_RSP_ACK)
				data->entered_svid = PD_VDO_VID(vdm_header);

			tcpci_partner_send_data_msg(data, PD_DATA_VENDOR_DEF,
						    data->enter_mode_vdm,
						    data->enter_mode_vdos, 0);
		}
		atomic_inc(&data->mode_enter_attempts);
		return TCPCI_PARTNER_COMMON_MSG_HANDLED;
	case CMD_EXIT_MODE:
		/* Only exit a SVID we know we entered */
		if (PD_VDO_VID(vdm_header) == data->entered_svid) {
			uint32_t response_vdm_header;

			response_vdm_header =
				VDO(PD_VDO_VID(vdm_header), true,
				    VDO_CMDT(CMDT_RSP_ACK) | CMD_EXIT_MODE);
			tcpci_partner_send_data_msg(data, PD_DATA_VENDOR_DEF,
						    &response_vdm_header, 1, 0);
		} else {
			uint32_t response_vdm_header;

			response_vdm_header =
				VDO(PD_VDO_VID(vdm_header), true,
				    VDO_CMDT(CMDT_RSP_NAK) | CMD_EXIT_MODE);
			tcpci_partner_send_data_msg(data, PD_DATA_VENDOR_DEF,
						    &response_vdm_header, 1, 0);
		}
		data->displayport_configured = false;
		return TCPCI_PARTNER_COMMON_MSG_HANDLED;
	case CMD_DP_STATUS:
		if (data->dp_status_vdos > 0 &&
		    (PD_VDO_VID(vdm_header) == USB_SID_DISPLAYPORT)) {
			tcpci_partner_send_data_msg(data, PD_DATA_VENDOR_DEF,
						    data->dp_status_vdm,
						    data->dp_status_vdos, 0);
		}
		return TCPCI_PARTNER_COMMON_MSG_HANDLED;
	case CMD_DP_CONFIG:
		if (data->dp_config_vdos > 0 &&
		    (PD_VDO_VID(vdm_header) == USB_SID_DISPLAYPORT)) {
			tcpci_partner_send_data_msg(data, PD_DATA_VENDOR_DEF,
						    data->dp_config_vdm,
						    data->dp_config_vdos, 0);
			data->displayport_configured = true;
		}
		return TCPCI_PARTNER_COMMON_MSG_HANDLED;
	default:
		/* TCPCI r. 2.0: Ignore unsupported commands. */
		return TCPCI_PARTNER_COMMON_MSG_HANDLED;
	}
}

static enum tcpci_partner_handler_res
tcpci_partner_enter_usb_handler(struct tcpci_partner_data *data,
				const struct tcpci_emul_msg *message)
{
	/*
	 * Validate received Enter_USB message against EUDO contents in
	 * tcpci_partner_data.
	 *
	 * TODO(b/260095516): This support needs to be expanded to validate the
	 * message contents, in a bit field basis. Currently, using this field
	 * as simple ACCEPT/REJECT criteria. If this value is 0 (default case),
	 * then ACCEPT this message, else reject it.
	 */
	if (data->enter_usb_accept)
		tcpci_partner_send_control_msg(data, PD_CTRL_ACCEPT, 0);
	else
		tcpci_partner_send_control_msg(data, PD_CTRL_REJECT, 0);

	return TCPCI_PARTNER_COMMON_MSG_HANDLED;
}

static enum tcpci_partner_handler_res
tcpci_partner_common_cable_handler(struct tcpci_partner_data *data,
				   const struct tcpci_emul_msg *message,
				   enum tcpci_msg_type sop_type)
{
	uint32_t vdm_header = sys_get_le32(message->buf + TCPCI_MSG_HEADER_LEN);
	uint32_t response_vdm_header;
	uint16_t header = sys_get_le16(&message->buf[0]);

	/* TODO(b/243151272): Add soft reset support */
	/* Ensure we are replying to a VDM */
	if (PD_HEADER_CNT(header) == 0 ||
	    PD_HEADER_TYPE(header) != PD_DATA_VENDOR_DEF ||
	    PD_HEADER_EXT(header) != 0)
		return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;

	/*
	 * Ignore any VDMs which are not sent by an initiator.  As a cable, we
	 * never expect to be the initiator processing ACKs.
	 * TODO(b/225397796): Validate VDM fields more thoroughly.
	 */
	if (PD_VDO_CMDT(vdm_header) != CMDT_INIT || !PD_VDO_SVDM(vdm_header)) {
		return TCPCI_PARTNER_COMMON_MSG_HANDLED;
	}

	/* If we have no cable, we must not GoodCRC */
	if (data->cable == NULL)
		return TCPCI_PARTNER_COMMON_MSG_NO_GOODCRC;

	/* TODO(b/243151272): Add SOP'' support */
	if (sop_type == TCPCI_MSG_SOP_PRIME_PRIME) {
		return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
	}

	switch (PD_VDO_CMD(vdm_header)) {
	case CMD_DISCOVER_IDENT:
		if (data->cable->identity_vdos > 0) {
			tcpci_cable_send_data_msg(data, PD_DATA_VENDOR_DEF,
						  data->cable->identity_vdm,
						  data->cable->identity_vdos,
						  TCPCI_MSG_SOP_PRIME, 0);
			return TCPCI_PARTNER_COMMON_MSG_HANDLED;
		}
		/* A cable with no identity shouldn't GoodCRC */
		return TCPCI_PARTNER_COMMON_MSG_NO_GOODCRC;
	case CMD_DISCOVER_SVID:
		if (data->cable->svids_vdos > 0) {
			tcpci_cable_send_data_msg(data, PD_DATA_VENDOR_DEF,
						  data->cable->svids_vdm,
						  data->cable->svids_vdos,
						  TCPCI_MSG_SOP_PRIME, 0);
		}
		return TCPCI_PARTNER_COMMON_MSG_HANDLED;
	case CMD_DISCOVER_MODES:
		if (data->cable->modes_vdos > 0) {
			tcpci_cable_send_data_msg(data, PD_DATA_VENDOR_DEF,
						  data->cable->modes_vdm,
						  data->cable->modes_vdos,
						  TCPCI_MSG_SOP_PRIME, 0);
		}
		return TCPCI_PARTNER_COMMON_MSG_HANDLED;
	default:
		/*
		 * Cable must support VDMs, so generate a NAK on unfamiliar
		 * commands
		 */
		response_vdm_header =
			VDO(PD_VDO_VID(vdm_header), true,
			    VDO_CMDT(CMDT_RSP_NAK) | PD_VDO_CMD(vdm_header));
		tcpci_cable_send_data_msg(data, PD_DATA_VENDOR_DEF,
					  &response_vdm_header, 1, sop_type, 0);

		return TCPCI_PARTNER_COMMON_MSG_HANDLED;
	}
}

/**
 * @brief Handle a received Battery Capability message from the TCPC. Save the
 *        contents to the emulator data struct for analysis.
 *
 * @param data Emulator state
 * @param message Received PD message
 * @return enum tcpci_partner_handler_res
 */
static enum tcpci_partner_handler_res
tcpci_partner_common_battery_capability_handler(
	struct tcpci_partner_data *data, const struct tcpci_emul_msg *message)
{
	uint16_t header = sys_get_le16(&message->buf[0]);
	uint16_t ext_header = sys_get_le16(&message->buf[2]);

	/* Validate message header */
	__ASSERT(PD_HEADER_TYPE(header) == PD_EXT_BATTERY_CAP,
		 "wrong message type");
	__ASSERT(PD_EXT_HEADER_DATA_SIZE(ext_header) == 9,
		 "Data size mismatch");

	int index = data->battery_capabilities.index;

	data->battery_capabilities.index = -1;

	if (index < 0) {
		LOG_ERR("Received a Battery Capability message but it was "
			"never requested");
		return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
	}

	__ASSERT(index < PD_BATT_MAX, "Battery index out of range");

	data->battery_capabilities.bcdb[index] = (struct pd_bcdb){
		.vid = sys_get_le16(&message->buf[4]),
		.pid = sys_get_le16(&message->buf[6]),
		.design_cap = sys_get_le16(&message->buf[8]),
		.last_full_charge_cap = sys_get_le16(&message->buf[10]),
		.battery_type = message->buf[12],
	};

	data->battery_capabilities.have_response[index] = true;

	LOG_INF("Saved data for battery index (%d): vid=%04x, pid=%04x, "
		"cap=%u, last_cap=%u, type=%02x",
		index, data->battery_capabilities.bcdb[index].vid,
		data->battery_capabilities.bcdb[index].pid,
		data->battery_capabilities.bcdb[index].design_cap,
		data->battery_capabilities.bcdb[index].last_full_charge_cap,
		data->battery_capabilities.bcdb[index].battery_type);

	return TCPCI_PARTNER_COMMON_MSG_HANDLED;
}

static void tcpci_partner_common_set_vconn(struct tcpci_partner_data *data,
					   enum pd_vconn_role role)
{
	data->vconn_role = role;
}

/**
 * @brief Handle VCONN_SWAP message
 *
 * @return enum tcpci_partner_handler_res
 */
static enum tcpci_partner_handler_res
tcpci_partner_common_vconn_swap_handler(struct tcpci_partner_data *data)
{
	tcpci_partner_common_set_ams_ctrl_msg(data, PD_CTRL_VCONN_SWAP);

	tcpci_partner_send_control_msg(data, data->vcs_response, 0);

	if (data->vcs_response != PD_CTRL_ACCEPT) {
		tcpci_partner_common_clear_ams_ctrl_msg(data);
		return TCPCI_PARTNER_COMMON_MSG_HANDLED;
	}

	if (data->vconn_role == PD_ROLE_VCONN_OFF) {
		tcpci_partner_common_set_vconn(data, PD_ROLE_VCONN_SRC);
	}

	/* PS ready after 15 ms */
	tcpci_partner_send_control_msg(data, PD_CTRL_PS_RDY, 15);
	return TCPCI_PARTNER_COMMON_MSG_HANDLED;
}

static enum tcpci_partner_handler_res
tcpci_partner_common_ps_rdy_vconn_swap_handler(struct tcpci_partner_data *data)
{
	tcpci_partner_common_clear_ams_ctrl_msg(data);

	if (data->vconn_role == PD_ROLE_VCONN_SRC) {
		tcpci_partner_common_set_vconn(data, PD_ROLE_VCONN_OFF);
	}

	return TCPCI_PARTNER_COMMON_MSG_HANDLED;
}

void tcpci_partner_common_swap_data_role(struct tcpci_partner_data *data)
{
	if (data->data_role == PD_ROLE_UFP) {
		data->data_role = PD_ROLE_DFP;
	} else if (data->data_role == PD_ROLE_DFP) {
		data->data_role = PD_ROLE_UFP;
	} else {
		/* PD_ROLE_DISCONNECTED - do nothing */
	}
}

/**
 * @brief Handle Incoming DR_SWAP request
 *
 * @return enum tcpci_partner_handler_res
 */
static enum tcpci_partner_handler_res
tcpci_partner_common_dr_swap_handler(struct tcpci_partner_data *data)
{
	enum pd_ctrl_msg_type response = PD_CTRL_REJECT;

	if ((data->data_role == PD_ROLE_DFP && data->drs_to_ufp_supported) ||
	    (data->data_role == PD_ROLE_UFP && data->drs_to_dfp_supported))
		response = PD_CTRL_ACCEPT;
	tcpci_partner_send_control_msg(data, response, 0);
	if (response == PD_CTRL_ACCEPT)
		tcpci_partner_common_swap_data_role(data);

	return TCPCI_PARTNER_COMMON_MSG_HANDLED;
}

static enum tcpci_partner_handler_res
tcpci_partner_common_accept_dr_swap_handler(struct tcpci_partner_data *data)
{
	tcpci_partner_common_clear_ams_ctrl_msg(data);

	tcpci_partner_common_swap_data_role(data);

	return TCPCI_PARTNER_COMMON_MSG_HANDLED;
}

static enum tcpci_partner_handler_res
tcpci_partner_common_accept_vconn_swap_handler(struct tcpci_partner_data *data)
{
	if (data->vconn_role == PD_ROLE_VCONN_SRC) {
		/* TODO: Wait for PS_RDY. */
	} else {
		/* VCONN Swap from off to VCONN Source means the partner sends
		 * the first PS_RDY after turning on VCONN.
		 */
		tcpci_partner_common_set_vconn(data, PD_ROLE_VCONN_ON);
		tcpci_partner_send_control_msg(data, PD_CTRL_PS_RDY, 15);
		tcpci_partner_common_clear_ams_ctrl_msg(data);
		/* Strictly speaking, the AMS isn't over until the partner
		 * receives GoodCRC for the PS_RDY.
		 */
	}

	return TCPCI_PARTNER_COMMON_MSG_HANDLED;
}

static enum tcpci_partner_handler_res
tcpi_drp_emul_ps_rdy_handler(struct tcpci_partner_data *data)
{
	switch (data->cur_ams_ctrl_req) {
	case PD_CTRL_VCONN_SWAP:
		return tcpci_partner_common_ps_rdy_vconn_swap_handler(data);

	default:
		LOG_ERR("Unhandled current_req=%u in PS_RDY",
			data->cur_ams_ctrl_req);
		return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
	}
}

static enum tcpci_partner_handler_res
tcpi_partner_common_handle_accept(struct tcpci_partner_data *data)
{
	switch (data->cur_ams_ctrl_req) {
	case PD_CTRL_VCONN_SWAP:
		data->cur_ams_ctrl_req = PD_CTRL_INVALID;
		return TCPCI_PARTNER_COMMON_MSG_HANDLED;

	default:
		LOG_ERR("Unhandled current_req=%u in ACCEPT",
			data->cur_ams_ctrl_req);
		return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
	}
}

static enum tcpci_partner_handler_res
tcpci_partner_common_handle_reject(struct tcpci_partner_data *data)
{
	switch (data->cur_ams_ctrl_req) {
	case PD_CTRL_DR_SWAP:
		return tcpci_partner_common_accept_dr_swap_handler(data);
	case PD_CTRL_VCONN_SWAP:
		return tcpci_partner_common_accept_vconn_swap_handler(data);

	default:
		LOG_ERR("Unhandled current_req=%u in ACCEPT",
			data->cur_ams_ctrl_req);
		return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
	}
}

/**
 * @brief Common handler for TCPCI SOP messages. Only some messages are handled
 *        here. It is expected that extensions cover other relevant messages.
 *
 * @param data Pointer to TCPCI partner emulator
 * @param tx_msg Message received by partner emulator
 *
 * @param TCPCI_PARTNER_COMMON_MSG_HANDLED Message was handled by common code
 * @param TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED Message wasn't handled
 * @param TCPCI_PARTNER_COMMON_MSG_HARD_RESET Message was handled by sending
 *                                            hard reset
 */
static enum tcpci_partner_handler_res
tcpci_partner_common_sop_msg_handler(struct tcpci_partner_data *data,
				     const struct tcpci_emul_msg *tx_msg)
{
	struct tcpci_partner_extension *ext;
	uint16_t header;
	int msg_type;

	LOG_HEXDUMP_DBG(tx_msg->buf, tx_msg->cnt,
			"USB-C partner emulator received message");

	header = sys_get_le16(tx_msg->buf);
	msg_type = PD_HEADER_TYPE(header);

	if (PD_HEADER_ID(header) == data->sop_recv_msg_id &&
	    msg_type != PD_CTRL_SOFT_RESET) {
		/* Repeated message mark as handled */
		return TCPCI_PARTNER_COMMON_MSG_HANDLED;
	}
	data->sop_recv_msg_id = PD_HEADER_ID(header);

	if (PD_HEADER_EXT(header)) {
		/* Extended message */

		if (PD_HEADER_REV(header) < PD_REV30) {
			LOG_ERR("Received extended message but current PD rev "
				"(0x%x) does not support them.",
				PD_HEADER_REV(header));
			return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
		}

		switch (PD_HEADER_TYPE(header)) {
		case PD_EXT_GET_BATTERY_CAP:
			/* Not implemented */
			LOG_INF("Got PD_EXT_GET_BATTERY_CAP");
			return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
		case PD_EXT_BATTERY_CAP:
			/* Received a Battery Capabilities response */
			LOG_INF("Got PD_EXT_BATTERY_CAP");

			return tcpci_partner_common_battery_capability_handler(
				data, tx_msg);
		default:
			return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
		}
	}

	if (PD_HEADER_CNT(header)) {
		/* Data message */
		switch (PD_HEADER_TYPE(header)) {
		case PD_DATA_VENDOR_DEF:
			return tcpci_partner_common_vdm_handler(data, tx_msg);
		case PD_DATA_ENTER_USB:
			return tcpci_partner_enter_usb_handler(data, tx_msg);
		default:
			/* No other common handlers for data messages */
			return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
		}
	}

	if (data->common_handler_masked & BIT(msg_type)) {
		/* This message type is masked from common handler */
		return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
	}

	/* Handle control message */
	switch (PD_HEADER_TYPE(header)) {
	case PD_CTRL_SOFT_RESET:
		data->sop_msg_id = 0;
		tcpci_partner_send_control_msg(data, PD_CTRL_ACCEPT, 0);

		for (ext = data->extensions; ext != NULL; ext = ext->next) {
			if (ext->ops->soft_reset) {
				ext->ops->soft_reset(ext, data);
			}
		}

		return TCPCI_PARTNER_COMMON_MSG_HANDLED;

	case PD_CTRL_VCONN_SWAP:
		return tcpci_partner_common_vconn_swap_handler(data);

	case PD_CTRL_DR_SWAP:
		return tcpci_partner_common_dr_swap_handler(data);

	case PD_CTRL_PS_RDY:
		return tcpi_drp_emul_ps_rdy_handler(data);

	case PD_CTRL_REJECT:
		if (data->in_soft_reset) {
			tcpci_partner_stop_sender_response_timer(data);
			tcpci_partner_common_send_hard_reset(data);

			return TCPCI_PARTNER_COMMON_MSG_HARD_RESET;
		} else if (data->cur_ams_ctrl_req != PD_CTRL_INVALID) {
			if (tcpci_partner_common_handle_reject(data) ==
			    TCPCI_PARTNER_COMMON_MSG_HANDLED)
				return TCPCI_PARTNER_COMMON_MSG_HANDLED;
		}

		tcpci_partner_common_clear_ams_ctrl_msg(data);

		__fallthrough;
	case PD_CTRL_ACCEPT:
		if (data->wait_for_response) {
			if (data->in_soft_reset) {
				/*
				 * Accept is response to soft reset send by
				 * common code. It is handled here
				 */
				tcpci_partner_stop_sender_response_timer(data);
				data->in_soft_reset = false;

				return TCPCI_PARTNER_COMMON_MSG_HANDLED;
			}
			/*
			 * Accept/reject is expected message and emulator code
			 * should handle it
			 */
			return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
		}

		if (data->cur_ams_ctrl_req != PD_CTRL_INVALID) {
			if (tcpi_partner_common_handle_accept(data) ==
			    TCPCI_PARTNER_COMMON_MSG_HANDLED) {
				return TCPCI_PARTNER_COMMON_MSG_HANDLED;
			}
		}

		/* Unexpected message - trigger soft reset */
		tcpci_partner_common_send_soft_reset(data);

		return TCPCI_PARTNER_COMMON_MSG_HANDLED;
	case PD_CTRL_DATA_RESET:
		/*
		 * Send Accept/Reject message
		 * TODO(b/260095516): To fully exercise this code path, there
		 * needs to be a mechanism (trigger) to either accept or reject
		 * this message.
		 */
		tcpci_partner_send_control_msg(data, PD_CTRL_ACCEPT, 0);
		return TCPCI_PARTNER_COMMON_MSG_HANDLED;
	case PD_CTRL_DATA_RESET_COMPLETE:
		/* There is no expected reply message from the port parter */
		return TCPCI_PARTNER_COMMON_MSG_HANDLED;
	}

	return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
}

void tcpci_partner_common_handler_mask_msg(struct tcpci_partner_data *data,
					   enum pd_ctrl_msg_type type,
					   bool enable)
{
	if (enable) {
		data->common_handler_masked |= BIT(type);
	} else {
		data->common_handler_masked &= ~BIT(type);
	}
}

void tcpci_partner_set_discovery_info(struct tcpci_partner_data *data,
				      int identity_vdos, uint32_t *identity_vdm,
				      int svids_vdos, uint32_t *svids_vdm,
				      int modes_vdos, uint32_t *modes_vdm)
{
	memset(data->identity_vdm, 0, sizeof(data->identity_vdm));
	memset(data->svids_vdm, 0, sizeof(data->svids_vdm));
	memset(data->modes_vdm, 0, sizeof(data->modes_vdm));

	data->identity_vdos = identity_vdos;
	memcpy(data->identity_vdm, identity_vdm,
	       identity_vdos * sizeof(*identity_vdm));
	data->svids_vdos = svids_vdos;
	memcpy(data->svids_vdm, svids_vdm, svids_vdos * sizeof(*svids_vdm));
	data->modes_vdos = modes_vdos;
	memcpy(data->modes_vdm, modes_vdm, modes_vdos * sizeof(*modes_vdm));
}

static void tcpci_partner_common_control_change(struct tcpci_partner_data *data)
{
	const struct emul *tcpci_emul = data->tcpci_emul;
	uint16_t role_control;

	tcpci_emul_get_reg(tcpci_emul, TCPC_REG_ROLE_CTRL, &role_control);
	data->tcpm_cc1 = TCPC_REG_ROLE_CTRL_CC1(role_control);
	data->tcpm_cc1 = TCPC_REG_ROLE_CTRL_CC2(role_control);
}

void tcpci_partner_common_disconnect(struct tcpci_partner_data *data)
{
	tcpci_partner_clear_msg_queue(data);
	tcpci_partner_stop_sender_response_timer(data);
	data->tcpci_emul = NULL;
	data->displayport_configured = false;
	data->entered_svid = 0;
	atomic_clear(&data->mode_enter_attempts);
}

int tcpci_partner_common_enable_pd_logging(struct tcpci_partner_data *data,
					   bool enable)
{
	int ret;

	ret = k_mutex_lock(&data->msg_log_mutex, K_FOREVER);
	if (ret) {
		return ret;
	}

	data->collect_msg_log = enable;

	k_mutex_unlock(&data->msg_log_mutex);
	return 0;
}

/** Names of senders used while printing logged PD messages */
static char *tcpci_partner_sender_names[] = {
	[TCPCI_PARTNER_SENDER_PARTNER] = "partner emulator",
	[TCPCI_PARTNER_SENDER_TCPM] = "TCPM"
};

/**
 * @brief Write string to the buffer starting from @p start byte.
 *
 * @param buf Pointer to the buffer
 * @param buf_len Total length of the buffer
 * @param start Byte from which start to write
 * @param fmt Format strnig
 * @param ... printf like arguments for format string
 *
 * @return Number of written bytes
 */
static __printf_like(4, 5) int tcpci_partner_print_to_buf(char *buf,
							  const int buf_len,
							  int start,
							  const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = vsnprintk(buf + start, buf_len - start, fmt, ap);
	va_end(ap);

	if (ret <= 0) {
		LOG_ERR("vsnprintk failed %d", ret);
		ret = 0;
	}

	return ret;
}

void tcpci_partner_common_print_logged_msgs(struct tcpci_partner_data *data)
{
	struct tcpci_partner_log_msg *msg;
	const int max_pd_msg_log_len = 200;
	const int buf_len = 8192;
	uint16_t header;
	char buf[buf_len];
	int chars_in = 0;
	int ret;
	int i;

	ret = k_mutex_lock(&data->msg_log_mutex, K_FOREVER);
	if (ret) {
		return;
	}

	chars_in += tcpci_partner_print_to_buf(buf, buf_len, chars_in,
					       "===PD messages log:\n");

	SYS_SLIST_FOR_EACH_CONTAINER(&data->msg_log, msg, node)
	{
		/*
		 * If there is too many messages to keep them in local buffer,
		 * accept possibility of lines interleaving on console and print
		 * buffer to console.
		 */
		if (chars_in >= buf_len - max_pd_msg_log_len) {
			LOG_PRINTK("%s", buf);
			chars_in = 0;
		}
		chars_in += tcpci_partner_print_to_buf(
			buf, buf_len, chars_in,
			"\tAt %lld Msg SOP %d from %s (status 0x%x):\n",
			msg->time, msg->sop,
			tcpci_partner_sender_names[msg->sender], msg->status);
		header = sys_get_le16(msg->buf);
		chars_in += tcpci_partner_print_to_buf(
			buf, buf_len, chars_in,
			"\t\text=%d;cnt=%d;id=%d;pr=%d;dr=%d;rev=%d;type=%d\n",
			PD_HEADER_EXT(header), PD_HEADER_CNT(header),
			PD_HEADER_ID(header), PD_HEADER_PROLE(header),
			PD_HEADER_DROLE(header), PD_HEADER_REV(header),
			PD_HEADER_TYPE(header));
		chars_in += tcpci_partner_print_to_buf(buf, buf_len, chars_in,
						       "\t\t");
		for (i = 0; i < msg->cnt; i++) {
			chars_in += tcpci_partner_print_to_buf(
				buf, buf_len, chars_in, "%02x ", msg->buf[i]);
		}
		chars_in += tcpci_partner_print_to_buf(buf, buf_len, chars_in,
						       "\n");
	}
	LOG_PRINTK("%s===\n", buf);

	k_mutex_unlock(&data->msg_log_mutex);
}

void tcpci_partner_common_clear_logged_msgs(struct tcpci_partner_data *data)
{
	struct tcpci_partner_log_msg *msg;
	int ret;

	ret = k_mutex_lock(&data->msg_log_mutex, K_FOREVER);
	if (ret) {
		return;
	}

	while (!sys_slist_is_empty(&data->msg_log)) {
		msg = CONTAINER_OF(sys_slist_get_not_empty(&data->msg_log),
				   struct tcpci_partner_log_msg, node);
		free(msg->buf);
		free(msg);
	}

	k_mutex_unlock(&data->msg_log_mutex);
}

/**
 * @brief Sets cur_ams_ctrl_req to msg_type to track current request
 *
 * @param data          Pointer to TCPCI partner data
 * @param msg_type      enum pd_ctrl_msg_type
 */
void tcpci_partner_common_set_ams_ctrl_msg(struct tcpci_partner_data *data,
					   enum pd_ctrl_msg_type msg_type)
{
	/* TODO(b/307386769): This assert seems to leave a mutex locked in
	 * i2c_controller.
	 */
	/* Make sure we handle one CTRL request at a time */
	zassert_equal(data->cur_ams_ctrl_req, PD_CTRL_INVALID,
		      "More than one CTRL msg handled in parallel"
		      " cur_ams_ctrl_req=%d, msg_type=%d",
		      data->cur_ams_ctrl_req, msg_type);
	data->cur_ams_ctrl_req = msg_type;
}

/**
 * @brief Sets cur_ams_ctrl_req to INVALID
 *
 * @param data          Pointer to TCPCI partner data
 */
void tcpci_partner_common_clear_ams_ctrl_msg(struct tcpci_partner_data *data)
{
	data->cur_ams_ctrl_req = PD_CTRL_INVALID;
}

void tcpci_partner_received_msg_status(struct tcpci_partner_data *data,
				       enum tcpci_emul_tx_status status)
{
	tcpci_emul_partner_msg_status(data->tcpci_emul, status);

	if (data->received_msg_status == NULL) {
		return;
	}
	/*
	 * Status of each received message should be reported to TCPCI emulator
	 * only once
	 */
	if (*data->received_msg_status != TCPCI_EMUL_TX_UNKNOWN) {
		LOG_WRN("Changing status of received message more than once");
	}
	*data->received_msg_status = status;
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
static void tcpci_partner_transmit_op(const struct emul *emul,
				      const struct tcpci_emul_partner_ops *ops,
				      const struct tcpci_emul_msg *tx_msg,
				      enum tcpci_msg_type type, int retry)
{
	struct tcpci_partner_data *data =
		CONTAINER_OF(ops, struct tcpci_partner_data, ops);
	enum tcpci_partner_handler_res processed;
	struct tcpci_partner_extension *ext;
	int ret;

	data->received_msg_status = tcpci_partner_log_msg(
		data, tx_msg, TCPCI_PARTNER_SENDER_TCPM, TCPCI_EMUL_TX_UNKNOWN);

	ret = k_mutex_lock(&data->transmit_mutex, K_FOREVER);
	if (ret) {
		LOG_ERR("Failed to get partner mutex");
		/* Inform TCPM that message send failed */
		if (type != TCPCI_MSG_TX_HARD_RESET &&
		    type != TCPCI_MSG_CABLE_RESET) {
			tcpci_partner_received_msg_status(data,
							  TCPCI_EMUL_TX_FAILED);
		}
		return;
	}

	/* Handle hard reset */
	if (type == TCPCI_MSG_TX_HARD_RESET) {
		tcpci_partner_common_hard_reset(data);
		goto message_handled;
	}

	/* Skip handling of non-SOP/SOP'/SOP'' messages */
	if (type > TCPCI_MSG_SOP_PRIME_PRIME) {
		/* Never send GoodCRC for cable reset */
		if (data->send_goodcrc && type != TCPCI_MSG_CABLE_RESET) {
			tcpci_partner_received_msg_status(
				data, TCPCI_EMUL_TX_SUCCESS);
		}
		goto message_handled;
	}

	/* Call common SOP handler */
	if (type == TCPCI_MSG_SOP) {
		processed = tcpci_partner_common_sop_msg_handler(data, tx_msg);
	} else {
		processed =
			tcpci_partner_common_cable_handler(data, tx_msg, type);
	}
	if (processed == TCPCI_PARTNER_COMMON_MSG_NO_GOODCRC) {
		/*
		 * Fail message send if common handler knows message shouldn't
		 * transit successfully.
		 */
		tcpci_partner_received_msg_status(data, TCPCI_EMUL_TX_FAILED);
		goto message_handled;
	} else if (data->send_goodcrc ||
		   processed != TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED) {
		/* Always send GoodCRC for messages handled by common handler */
		tcpci_partner_received_msg_status(data, TCPCI_EMUL_TX_SUCCESS);
	}

	/* Continue only for unhandled messages */
	if (processed != TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED) {
		goto message_handled;
	}

	for (ext = data->extensions; ext != NULL; ext = ext->next) {
		if (ext->ops->sop_msg_handler == NULL) {
			continue;
		}
		processed = ext->ops->sop_msg_handler(ext, data, tx_msg);
		if (processed == TCPCI_PARTNER_COMMON_MSG_HANDLED) {
			goto message_handled;
		}
	}

	/* Send reject for not handled messages (PD rev 2.0) */
	tcpci_partner_send_control_msg(data, PD_CTRL_REJECT, 0);

message_handled:
	k_mutex_unlock(&data->transmit_mutex);
}

/**
 * @brief Function called when TCPM consumes message. Free message that is no
 *        longer needed.
 *
 * @param emul Pointer to TCPCI emulator
 * @param ops Pointer to partner operations structure
 * @param rx_msg Message that was consumed by TCPM
 */
static void
tcpci_partner_rx_consumed_op(const struct emul *emul,
			     const struct tcpci_emul_partner_ops *ops,
			     const struct tcpci_emul_msg *rx_msg)
{
	struct tcpci_partner_msg *msg =
		CONTAINER_OF(rx_msg, struct tcpci_partner_msg, msg);

	tcpci_partner_free_msg(msg);
}

static void
tcpci_partner_control_change_op(const struct emul *emul,
				const struct tcpci_emul_partner_ops *ops)
{
	struct tcpci_partner_data *data =
		CONTAINER_OF(ops, struct tcpci_partner_data, ops);
	struct tcpci_partner_extension *ext;

	tcpci_partner_common_control_change(data);
	for (ext = data->extensions; ext != NULL; ext = ext->next) {
		if (ext->ops->control_change) {
			ext->ops->control_change(ext, data);
		}
	}
}

/**
 * @brief Function called when emulator is disconnected from TCPCI
 *
 * @param emul Pointer to TCPCI emulator
 * @param ops Pointer to partner operations structure
 */
static void
tcpci_partner_disconnect_op(const struct emul *emul,
			    const struct tcpci_emul_partner_ops *ops)
{
	struct tcpci_partner_data *data =
		CONTAINER_OF(ops, struct tcpci_partner_data, ops);
	struct tcpci_partner_extension *ext;

	tcpci_partner_common_disconnect(data);
	for (ext = data->extensions; ext != NULL; ext = ext->next) {
		if (ext->ops->disconnect) {
			ext->ops->disconnect(ext, data);
		}
	}
}

int tcpci_partner_connect_to_tcpci(struct tcpci_partner_data *data,
				   const struct emul *tcpci_emul)
{
	struct tcpci_partner_extension *ext;
	int ret;

	data->tcpci_emul = tcpci_emul;

	for (ext = data->extensions; ext != NULL; ext = ext->next) {
		if (ext->ops->connect == NULL) {
			continue;
		}
		ret = ext->ops->connect(ext, data);
		if (ret) {
			data->tcpci_emul = NULL;
			return ret;
		}
	}

	/* Try to connect using current state of partner emulator */
	tcpci_emul_set_partner_ops(data->tcpci_emul, &data->ops);
	ret = tcpci_emul_connect_partner(data->tcpci_emul, data->power_role,
					 data->cc1, data->cc2, data->polarity);
	if (ret) {
		tcpci_emul_set_partner_ops(data->tcpci_emul, NULL);
		data->tcpci_emul = NULL;
	}

	/* Clear any received battery capability info */
	tcpci_partner_reset_battery_capability_state(data);

	return ret;
}

void tcpci_partner_reset_battery_capability_state(
	struct tcpci_partner_data *data)
{
	memset(&data->battery_capabilities, 0,
	       sizeof(data->battery_capabilities));
	data->battery_capabilities.index = -1;
}

void tcpci_partner_init(struct tcpci_partner_data *data, enum pd_rev_type rev)
{
	k_timer_init(&data->delayed_send, tcpci_partner_delayed_send_timer,
		     NULL);
	k_work_init_delayable(&data->sender_response_timeout,
			      tcpci_partner_sender_response_timeout);
	sys_slist_init(&data->to_send);
	k_mutex_init(&data->to_send_mutex);
	k_mutex_init(&data->transmit_mutex);
	sys_slist_init(&data->msg_log);
	k_mutex_init(&data->msg_log_mutex);
	data->collect_msg_log = false;
	tcpci_partner_common_reset(data);
	data->tcpm_timeouts = 0;
	data->identity_vdos = 0;
	data->svids_vdos = 0;
	data->modes_vdos = 0;
	data->rmdo = 0;
	data->enter_usb_accept = false;

	tcpci_partner_common_clear_ams_ctrl_msg(data);

	data->send_goodcrc = true;

	data->rev = rev;
	data->drs_to_dfp_supported = true;
	data->drs_to_ufp_supported = true;
	data->vcs_response = PD_CTRL_ACCEPT;

	data->ops.transmit = tcpci_partner_transmit_op;
	data->ops.rx_consumed = tcpci_partner_rx_consumed_op;
	data->ops.control_change = tcpci_partner_control_change_op;
	data->ops.disconnect = tcpci_partner_disconnect_op;
	data->displayport_configured = false;
	data->entered_svid = 0;
	atomic_clear(&data->mode_enter_attempts);

	/* Reset the data structure used to store battery capability responses
	 */
	tcpci_partner_reset_battery_capability_state(data);

	data->cable = NULL;
}

void tcpci_partner_set_drs_support(struct tcpci_partner_data *data,
				   bool drs_to_ufp_supported,
				   bool drs_to_dfp_supported)
{
	data->drs_to_ufp_supported = drs_to_ufp_supported;
	data->drs_to_dfp_supported = drs_to_dfp_supported;
}

void tcpci_partner_set_vcs_response(struct tcpci_partner_data *data,
				    enum pd_ctrl_msg_type vcs_response)
{
	data->vcs_response = vcs_response;
}
