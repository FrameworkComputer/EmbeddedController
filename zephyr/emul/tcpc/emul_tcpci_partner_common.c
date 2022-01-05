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
static void tcpci_partner_delayed_send(void *fifo_data)
{
	struct tcpci_partner_data *data =
		CONTAINER_OF(fifo_data, struct tcpci_partner_data, fifo_data);
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
		k_timer_start(&data->delayed_send, K_MSEC(delay), K_NO_WAIT);
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

/** Check description in emul_common_tcpci_partner.h */
void tcpci_partner_init(struct tcpci_partner_data *data)
{
	k_timer_init(&data->delayed_send, tcpci_partner_delayed_send_timer,
		     NULL);
	sys_slist_init(&data->to_send);
	k_mutex_init(&data->to_send_mutex);
}
