/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(tcpci_partner, CONFIG_TCPCI_EMUL_LOG_LEVEL);

#include <sys/byteorder.h>
#include <zephyr.h>

#include "common.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci.h"
#include "usb_pd.h"

/** Length of PDO, RDO and BIST request object in SOP message in bytes */
#define TCPCI_MSG_DO_LEN	4
/** Length of header in SOP message in bytes  */
#define TCPCI_MSG_HEADER_LEN	2

/** Check description in emul_common_tcpci_partner.h */
struct tcpci_partner_msg *tcpci_partner_alloc_msg(int data_objects)
{
	struct tcpci_partner_msg *new_msg;
	size_t size = TCPCI_MSG_HEADER_LEN + TCPCI_MSG_DO_LEN * data_objects;

	new_msg = k_malloc(sizeof(struct tcpci_partner_msg));
	if (new_msg == NULL) {
		return NULL;
	}

	new_msg->msg.buf = k_malloc(size);
	if (new_msg->msg.buf == NULL) {
		k_free(new_msg);
		return NULL;
	}

	/* Set default message type to SOP */
	new_msg->msg.type = TCPCI_MSG_SOP;
	/* TCPCI message size count include type byte */
	new_msg->msg.cnt = size + 1;
	new_msg->data_objects = data_objects;

	return new_msg;
}

/** Check description in emul_common_tcpci_partner.h */
void tcpci_partner_free_msg(struct tcpci_partner_msg *msg)
{
	k_free(msg->msg.buf);
	k_free(msg);
}

/** Check description in emul_common_tcpci_partner.h */
void tcpci_partner_set_header(struct tcpci_partner_data *data,
			      struct tcpci_partner_msg *msg)
{
	/* Header msg id has only 3 bits and wraps around after 8 messages */
	uint16_t msg_id = data->msg_id & 0x7;
	uint16_t header = PD_HEADER(msg->type, data->power_role,
				    data->data_role, msg_id, msg->data_objects,
				    data->rev, 0 /* ext */);
	data->msg_id++;

	msg->msg.buf[1] = (header >> 8) & 0xff;
	msg->msg.buf[0] = header & 0xff;
}

/**
 * @brief Work function which sends delayed messages
 *
 * @param work Pointer to work structure
 */
static void tcpci_partner_delayed_send(struct k_work *work)
{
	struct k_work_delayable *kwd = k_work_delayable_from_work(work);
	struct tcpci_partner_data *data =
		CONTAINER_OF(kwd, struct tcpci_partner_data, delayed_send);
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
			ret = tcpci_emul_add_rx_msg(data->tcpci_emul, &msg->msg,
						    true /* send alert */);
			if (ret) {
				tcpci_partner_free_msg(msg);
			}

			do {
				ret = k_mutex_lock(&data->to_send_mutex,
						   K_FOREVER);
			} while (ret);
		} else {
			k_work_reschedule(kwd, K_MSEC(msg->time - now));
			break;
		}
	}

	k_mutex_unlock(&data->to_send_mutex);
}

/** Check description in emul_common_tcpci_partner.h */
int tcpci_partner_send_msg(struct tcpci_partner_data *data,
			   struct tcpci_partner_msg *msg, uint64_t delay)
{
	struct tcpci_partner_msg *next_msg;
	struct tcpci_partner_msg *prev_msg;
	uint64_t now;
	int ret;

	if (delay == 0) {
		tcpci_partner_set_header(data, msg);
		ret = tcpci_emul_add_rx_msg(data->tcpci_emul, &msg->msg, true);
		if (ret) {
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

	prev_msg = SYS_SLIST_PEEK_HEAD_CONTAINER(&data->to_send, prev_msg,
						 node);
	/* Current message should be sent first */
	if (prev_msg == NULL || prev_msg->time > msg->time) {
		sys_slist_prepend(&data->to_send, &msg->node);
		k_work_reschedule(&data->delayed_send, K_MSEC(delay));
		k_mutex_unlock(&data->to_send_mutex);
		return 0;
	}

	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&data->to_send, prev_msg, next_msg,
					  node) {
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

/** Check description in emul_common_tcpci_partner.h */
int tcpci_partner_send_control_msg(struct tcpci_partner_data *data,
				   enum pd_ctrl_msg_type type,
				   uint64_t delay)
{
	struct tcpci_partner_msg *msg;

	msg = tcpci_partner_alloc_msg(0);
	if (msg == NULL) {
		return -ENOMEM;
	}

	msg->type = type;

	return tcpci_partner_send_msg(data, msg, delay);
}

/** Check description in emul_common_tcpci_partner.h */
int tcpci_partner_send_data_msg(struct tcpci_partner_data *data,
				enum pd_data_msg_type type,
				uint32_t *data_obj, int data_obj_num,
				uint64_t delay)
{
	struct tcpci_partner_msg *msg;
	int addr;

	msg = tcpci_partner_alloc_msg(data_obj_num);
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

/** Check description in emul_common_tcpci_partner.h */
int tcpci_partner_clear_msg_queue(struct tcpci_partner_data *data)
{
	struct tcpci_partner_msg *msg;
	int ret;

	k_work_cancel_delayable(&data->delayed_send);

	ret = k_mutex_lock(&data->to_send_mutex, K_FOREVER);
	if (ret) {
		return ret;
	}

	while (!sys_slist_is_empty(&data->to_send)) {
		msg = SYS_SLIST_CONTAINER(
				sys_slist_get_not_empty(&data->to_send),
				msg, node);
		tcpci_partner_free_msg(msg);
	}

	k_mutex_unlock(&data->to_send_mutex);

	return 0;
}

/** Check description in emul_common_tcpci_partner.h */
void tcpci_partner_init(struct tcpci_partner_data *data)
{
	k_work_init_delayable(&data->delayed_send, tcpci_partner_delayed_send);
	sys_slist_init(&data->to_send);
	k_mutex_init(&data->to_send_mutex);
}
