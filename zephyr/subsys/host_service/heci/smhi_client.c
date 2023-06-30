/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "heci.h"
#include "smhi_client.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(smhi, CONFIG_SMHI_LOG_LEVEL);

#define HECI_CLIENT_SMHI_GUID                                          \
	{                                                              \
		0xbb579a2e, 0xcc54, 0x4450,                            \
		{                                                      \
			0xb1, 0xd0, 0x5e, 0x75, 0x20, 0xdc, 0xad, 0x25 \
		}                                                      \
	}

#define SMHI_MAX_RX_SIZE 256
#define SMHI_STACK_SIZE 1600

static heci_rx_msg_t smhi_rx_msg;
static uint8_t smhi_rx_buffer[SMHI_MAX_RX_SIZE];
static uint8_t smhi_tx_buffer[SMHI_MAX_RX_SIZE];

static K_THREAD_STACK_DEFINE(smhi_stack, SMHI_STACK_SIZE);
static struct k_thread smhi_thread;

static K_SEM_DEFINE(smhi_event_sem, 0, 1);
static uint32_t smhi_event;
static uint32_t smhi_conn_id;
static uint32_t smhi_flag;

static void smhi_ret_version(struct smhi_msg_hdr_t *txhdr)
{
	LOG_DBG("get SMHI_GET_VERSION command");
	mrd_t m = { 0 };
	struct smhi_get_version_resp *resp =
		(struct smhi_get_version_resp *)(txhdr + 1);

	txhdr->command = SMHI_GET_VERSION;
	resp->major = SMHI_MAJOR_VERSION;
	resp->minor = SMHI_MINOR_VERSION;
	resp->hotfix = SMHI_HOTFIX_VERSION;
	resp->build = SMHI_BUILD_VERSION;

	m.buf = (void *)txhdr;
	m.len = sizeof(struct smhi_get_version_resp) +
		sizeof(struct smhi_msg_hdr_t);
	heci_send(smhi_conn_id, &m);
}

static void smhi_process_msg(uint8_t *buf)
{
	struct smhi_msg_hdr_t *rxhdr = (struct smhi_msg_hdr_t *)buf;
	struct smhi_msg_hdr_t *txhdr = (struct smhi_msg_hdr_t *)smhi_tx_buffer;

	txhdr->is_response = 1;
	txhdr->has_next = 0;
	txhdr->reserved = 0;
	txhdr->status = 0;

	LOG_DBG("smhi cmd =  %u", rxhdr->command);
	switch (rxhdr->command) {
	case SMHI_GET_VERSION: {
		smhi_ret_version(txhdr);
		break;
	}
	default:
		LOG_DBG("get unsupported SMHI command %d", rxhdr->command);
	}
}

static void smhi_event_callback(uint32_t event, void *arg)
{
	ARG_UNUSED(arg);
	smhi_event = event;
	k_sem_give(&smhi_event_sem);
}

static void smhi_task(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_DBG("enter");

	while (true) {
		k_sem_take(&smhi_event_sem, K_FOREVER);

		LOG_DBG("smhi new heci event %u", smhi_event);

		switch (smhi_event) {
		case HECI_EVENT_NEW_MSG:
			if (smhi_rx_msg.msg_lock != MSG_LOCKED) {
				LOG_ERR("invalid heci message");
				break;
			}

			if (smhi_rx_msg.type == HECI_CONNECT) {
				smhi_flag |= SMHI_CONN_FLAG;
				smhi_conn_id = smhi_rx_msg.connection_id;
				LOG_DBG("new conn: %u", smhi_conn_id);
			} else if (smhi_rx_msg.type == HECI_REQUEST) {
				smhi_process_msg(smhi_rx_msg.buffer);
			}

			/*
			 * send flow control after finishing one message,
			 * allow host to send new request
			 */
			heci_send_flow_control(smhi_conn_id);
			break;

		case HECI_EVENT_DISCONN:
			LOG_DBG("disconnect request conn %d", smhi_conn_id);
			heci_complete_disconnect(smhi_conn_id);
			smhi_flag &= (~SMHI_CONN_FLAG);
			break;

		default:
			LOG_ERR("wrong heci event %u", smhi_event);
			break;
		}
	}
}

static int smhi_init(void)
{
	int ret;
	heci_client_t smhi_client = { .protocol_id = HECI_CLIENT_SMHI_GUID,
				      .max_msg_size = SMHI_MAX_RX_SIZE,
				      .protocol_ver = 1,
				      .max_n_of_connections = 1,
				      .dma_header_length = 0,
				      .dma_enabled = 0,
				      .rx_buffer_len = SMHI_MAX_RX_SIZE,
				      .event_cb = smhi_event_callback };

	smhi_client.rx_msg = &smhi_rx_msg;
	smhi_client.rx_msg->buffer = smhi_rx_buffer;

	ret = heci_register(&smhi_client);
	if (ret) {
		LOG_ERR("failed to register smhi client %d", ret);
		return ret;
	}

	k_thread_create(&smhi_thread, smhi_stack,
			K_THREAD_STACK_SIZEOF(smhi_stack), smhi_task, NULL,
			NULL, NULL, K_PRIO_PREEMPT(11), 0, K_NO_WAIT);
	k_thread_name_set(&smhi_thread, "smhi_client");

	return 0;
}

SYS_INIT(smhi_init, APPLICATION, 95);
