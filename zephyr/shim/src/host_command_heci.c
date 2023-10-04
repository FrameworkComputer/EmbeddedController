/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "hooks.h"
#include "host_command.h"
#include "hwtimer.h"

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <heci.h>

LOG_MODULE_REGISTER(cros_ec_heci, LOG_LEVEL_INF);

#define CROS_EC_ISHTP_STACK_SIZE 1024

#define HECI_CLIENT_CROS_EC_ISH_GUID                                   \
	{                                                              \
		0x7b7154d0, 0x56f4, 0x4bdc,                            \
		{                                                      \
			0xb0, 0xd8, 0x9e, 0x7c, 0xda, 0xe0, 0xd6, 0xa0 \
		}                                                      \
	}

/*
 * If we hit response buffer size issues, we can increase this. This is the
 * current size of a single HECI packet.
 *
 * Aligning with other assumptions in host command stack, only a single host
 * command can be processed at a given time.
 */

struct cros_ec_ishtp_msg_hdr {
	uint8_t channel;
	uint8_t status;
	uint8_t id; /* Pairs up request and responses */
	uint8_t reserved;
} __ec_align4;

#define CROS_EC_ISHTP_MSG_HDR_SIZE sizeof(struct cros_ec_ishtp_msg_hdr)
#define CROS_EC_ISHTP_RX_BUF_SIZE 260
#define HECI_CROS_EC_REQUEST_MAX \
	(CROS_EC_ISHTP_RX_BUF_SIZE - CROS_EC_ISHTP_MSG_HDR_SIZE)
/*
 * Increase response_buffer size
 * some host command response messages use bigger space; so increase
 * the buffer size on par with EC, which is 256 bytes in total.
 * The size has to meet
 * HECI_CROS_EC_RESPONSE_BUF_SIZE >= CROS_EC_ISHTP_MSG_HDR_SIZE + 256.
 * Here 260 bytes is chosen.
 */
#define HECI_CROS_EC_RESPONSE_BUF_SIZE 260
#define HECI_CROS_EC_RESPONSE_MAX \
	(HECI_CROS_EC_RESPONSE_BUF_SIZE - CROS_EC_ISHTP_MSG_HDR_SIZE)
BUILD_ASSERT(HECI_CROS_EC_RESPONSE_BUF_SIZE >=
	     CROS_EC_ISHTP_MSG_HDR_SIZE + 256);
struct cros_ec_ishtp_msg {
	struct cros_ec_ishtp_msg_hdr hdr;
	uint8_t data[0];
} __ec_align4;

enum heci_cros_ec_channel {
	CROS_EC_COMMAND = 1, /* initiated from AP */
	CROS_MKBP_EVENT, /* initiated from EC */
};
static uint8_t response_buffer[HECI_CROS_EC_RESPONSE_BUF_SIZE] __aligned(4);
static struct host_packet heci_packet;

static struct heci_rx_msg_t cros_ec_rx_msg;
static uint8_t cros_ec_rx_buffer[CROS_EC_ISHTP_RX_BUF_SIZE];

static K_THREAD_STACK_DEFINE(cros_ec_ishtp_stack, CROS_EC_ISHTP_STACK_SIZE);
static struct k_thread cros_ec_ishtp_thread;

static K_SEM_DEFINE(cros_ec_ishtp_event_sem, 0, 1);
static uint32_t cros_ec_ishtp_event;
static uint32_t heci_cros_ec_conn_id;

int heci_send_mkbp_event(uint32_t *timestamp)
{
	struct cros_ec_ishtp_msg evt;
	struct mrd_t m = { 0 };

	evt.hdr.channel = CROS_MKBP_EVENT;
	evt.hdr.status = 0;
	m.buf = &evt;
	m.len = sizeof(evt);

	*timestamp = __hw_clock_source_read();
	return heci_send(heci_cros_ec_conn_id, &m) ? EC_SUCCESS :
						     EC_ERROR_UNKNOWN;
}

static void heci_send_hostcmd_response(struct host_packet *pkt)
{
	struct cros_ec_ishtp_msg *out =
		(struct cros_ec_ishtp_msg *)response_buffer;
	struct mrd_t m = { 0 };

	out->hdr.channel = CROS_EC_COMMAND;
	out->hdr.status = 0;
	/* id is already set in the receiving method */

	m.buf = out;
	m.len = pkt->response_size + CROS_EC_ISHTP_MSG_HDR_SIZE;

	if (!heci_send(heci_cros_ec_conn_id, &m)) {
		LOG_ERR("HC response failed");
	}

	heci_send_flow_control(heci_cros_ec_conn_id);
}

static void cros_ec_ishtp_process_msg(uint8_t *msg, const size_t msg_size)
{
	struct cros_ec_ishtp_msg *in = (void *)msg;
	struct cros_ec_ishtp_msg *out = (void *)response_buffer;

	if (in->hdr.channel != CROS_EC_COMMAND) {
		LOG_ERR("Unknown HECI packet 0x%02x", in->hdr.channel);
		return;
	}
	memset(&heci_packet, 0, sizeof(heci_packet));

	/* Copy over id from sender so they can pair up messages */
	out->hdr.id = in->hdr.id;

	heci_packet.send_response = heci_send_hostcmd_response;

	heci_packet.request = in->data;
	heci_packet.request_max = HECI_CROS_EC_REQUEST_MAX;
	heci_packet.request_size = msg_size - CROS_EC_ISHTP_MSG_HDR_SIZE;

	heci_packet.response = out->data;
	heci_packet.response_max = HECI_CROS_EC_RESPONSE_MAX;
	heci_packet.response_size = 0;

	heci_packet.driver_result = EC_RES_SUCCESS;
	if (IS_ENABLED(HAS_TASK_HOSTCMD))
		host_packet_receive(&heci_packet);
}

/**
 * Get protocol information
 */
static enum ec_status heci_get_protocol_info(struct host_cmd_handler_args *args)
{
	struct ec_response_get_protocol_info *r = args->response;

	memset(r, 0, sizeof(*r));
	r->protocol_versions = BIT(3);
	r->max_request_packet_size = HECI_CROS_EC_REQUEST_MAX;
	r->max_response_packet_size = HECI_CROS_EC_RESPONSE_MAX;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PROTOCOL_INFO, heci_get_protocol_info,
		     EC_VER_MASK(0));

static void cros_ec_ishtp_event_callback(uint32_t event, void *arg)
{
	ARG_UNUSED(arg);
	cros_ec_ishtp_event = event;
	k_sem_give(&cros_ec_ishtp_event_sem);
}

static void cros_ec_ishtp_task(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_DBG("Enter %s", __func__);

	while (true) {
		k_sem_take(&cros_ec_ishtp_event_sem, K_FOREVER);

		LOG_DBG("cros ec new heci event %u", cros_ec_ishtp_event);

		switch (cros_ec_ishtp_event) {
		case HECI_EVENT_NEW_MSG:
			if (cros_ec_rx_msg.msg_lock != MSG_LOCKED) {
				LOG_ERR("Invalid heci message");
				break;
			}

			if (cros_ec_rx_msg.type == HECI_CONNECT) {
				heci_cros_ec_conn_id =
					cros_ec_rx_msg.connection_id;
				heci_send_flow_control(heci_cros_ec_conn_id);
				LOG_DBG("heci cros ec new conn: %u",
					heci_cros_ec_conn_id);
			} else if (cros_ec_rx_msg.type == HECI_REQUEST) {
				cros_ec_ishtp_process_msg(
					cros_ec_rx_msg.buffer,
					cros_ec_rx_msg.length);
			}
			break;

		case HECI_EVENT_DISCONN:
			LOG_DBG("cros ec disconnect request conn %d",
				heci_cros_ec_conn_id);
			heci_complete_disconnect(heci_cros_ec_conn_id);
			break;

		default:
			LOG_ERR("cros ec wrong heci event %u",
				cros_ec_ishtp_event);
			break;
		}
	}
}

static int cros_ec_ishtp_client_init(void)
{
	int ret;

	struct heci_client_t cros_ec_ishtp_client = {
		.protocol_id = HECI_CLIENT_CROS_EC_ISH_GUID,
		.max_msg_size = CROS_EC_ISHTP_RX_BUF_SIZE,
		.protocol_ver = 1,
		.max_n_of_connections = 1,
		.dma_header_length = 0,
		.dma_enabled = 0,
		.rx_buffer_len = CROS_EC_ISHTP_RX_BUF_SIZE,
		.event_cb = cros_ec_ishtp_event_callback
	};

	cros_ec_ishtp_client.rx_msg = &cros_ec_rx_msg;
	cros_ec_ishtp_client.rx_msg->buffer = cros_ec_rx_buffer;

	ret = heci_register(&cros_ec_ishtp_client);
	if (ret) {
		LOG_ERR("failed to register cros ec client %d", ret);
		return ret;
	}

	k_thread_create(&cros_ec_ishtp_thread, cros_ec_ishtp_stack,
			K_THREAD_STACK_SIZEOF(cros_ec_ishtp_stack),
			cros_ec_ishtp_task, NULL, NULL, NULL, K_PRIO_PREEMPT(1),
			0, K_NO_WAIT);
	k_thread_name_set(&cros_ec_ishtp_thread, "cros_ec_ishtp_client");

	return 0;
}

SYS_INIT(cros_ec_ishtp_client_init, APPLICATION, 99);

static enum ec_status
host_command_host_sleep_event(struct host_cmd_handler_args *args)
{
	const struct ec_params_host_sleep_event_v1 *p = args->params;
	enum host_sleep_event state = p->sleep_event;

	switch (state) {
	case HOST_SLEEP_EVENT_S0IX_SUSPEND:
	case HOST_SLEEP_EVENT_S3_SUSPEND:
	case HOST_SLEEP_EVENT_S3_WAKEABLE_SUSPEND:
		LOG_INF("%s: suspend", __FILE__);
		hook_notify(HOOK_CHIPSET_SUSPEND);
		break;

	case HOST_SLEEP_EVENT_S0IX_RESUME:
	case HOST_SLEEP_EVENT_S3_RESUME:
		LOG_INF("%s: resume", __FILE__);
		hook_notify(HOOK_CHIPSET_RESUME);
		break;

	default:
		break;
	}

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_HOST_SLEEP_EVENT, host_command_host_sleep_event,
		     EC_VER_MASK(0) | EC_VER_MASK(1));
