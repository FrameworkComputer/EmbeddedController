/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(tcpci_partner, CONFIG_TCPCI_EMUL_LOG_LEVEL);

#include <zephyr.h>

#include "common.h"
#include "emul/emul_tcpci_partner_common.h"
#include "emul/emul_tcpci.h"
#include "usb_pd.h"

/** Check description in emul_common_tcpci_partner.h */
struct tcpci_partner_msg *tcpci_partner_alloc_msg(size_t size)
{
	struct tcpci_partner_msg *new_msg;

	new_msg = k_malloc(sizeof(struct tcpci_partner_msg));
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

/** Check description in emul_common_tcpci_partner.h */
void tcpci_partner_free_msg(struct tcpci_partner_msg *msg)
{
	k_free(msg->msg.buf);
	k_free(msg);
}

/** Check description in emul_common_tcpci_partner.h */
void tcpci_partner_set_header(struct tcpci_partner_data *data,
			      struct tcpci_partner_msg *msg,
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
static void tcpci_partner_delayed_send(struct k_work *work)
{
	struct k_work_delayable *kwd = k_work_delayable_from_work(work);
	struct tcpci_partner_data *data =
		CONTAINER_OF(kwd, struct tcpci_partner_data, delayed_send);
	struct tcpci_partner_msg *msg;
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
				tcpci_partner_free_msg(msg);
			}
		} else {
			k_work_reschedule(kwd, K_MSEC(msg->time - now));
			break;
		}
	}
}

/** Check description in emul_common_tcpci_partner.h */
int tcpci_partner_send_msg(struct tcpci_partner_data *data,
			   struct tcpci_partner_msg *msg, uint64_t delay)
{
	uint64_t now;
	int ec;

	if (delay == 0) {
		ec = tcpci_emul_add_rx_msg(data->tcpci_emul, &msg->msg, true);
		if (ec) {
			tcpci_partner_free_msg(msg);
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

/** Check description in emul_common_tcpci_partner.h */
int tcpci_partner_send_control_msg(struct tcpci_partner_data *data,
				   enum pd_ctrl_msg_type type,
				   uint64_t delay)
{
	struct tcpci_partner_msg *msg;

	msg = tcpci_partner_alloc_msg(2);
	if (msg == NULL) {
		return -ENOMEM;
	}

	tcpci_partner_set_header(data, msg, type, 0);

	/* Fill tcpci message structure */
	msg->msg.type = TCPCI_MSG_SOP;

	return tcpci_partner_send_msg(data, msg, delay);
}

/** Check description in emul_common_tcpci_partner.h */
void tcpci_partner_init(struct tcpci_partner_data *data)
{
	k_work_init_delayable(&data->delayed_send, tcpci_partner_delayed_send);
	k_fifo_init(&data->to_send);
}
