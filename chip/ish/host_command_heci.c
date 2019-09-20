/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "heci_client.h"
#include "host_command.h"
#include "host_command_heci.h"
#include "ipc_heci.h"
#include "ish_fwst.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTS(format, args...) cprints(CC_LPC, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_LPC, format, ## args)

#define HECI_CLIENT_CROS_EC_ISH_GUID { 0x7b7154d0, 0x56f4, 0x4bdc,\
			 { 0xb0, 0xd8, 0x9e, 0x7c, 0xda, 0xe0, 0xd6, 0xa0 } }

/* Handle for all heci cros_ec interactions */
static heci_handle_t heci_cros_ec_handle = HECI_INVALID_HANDLE;

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
	uint8_t id;	/* Pairs up request and responses */
	uint8_t reserved;
} __ec_align4;

#define CROS_EC_ISHTP_MSG_HDR_SIZE sizeof(struct cros_ec_ishtp_msg_hdr)
#define HECI_CROS_EC_RESPONSE_MAX \
	(HECI_IPC_PAYLOAD_SIZE - CROS_EC_ISHTP_MSG_HDR_SIZE)

struct cros_ec_ishtp_msg {
	struct cros_ec_ishtp_msg_hdr hdr;
	uint8_t data[0];
} __ec_align4;

enum heci_cros_ec_channel {
	CROS_EC_COMMAND = 1, /* initiated from AP */
	CROS_MKBP_EVENT = 2, /* initiated from EC */
};

static uint8_t response_buffer[IPC_MAX_PAYLOAD_SIZE] __aligned(4);
static struct host_packet heci_packet;

int heci_send_mkbp_event(uint32_t *timestamp)
{
	struct cros_ec_ishtp_msg evt;
	int rv;

	evt.hdr.channel = CROS_MKBP_EVENT;
	evt.hdr.status = 0;

	rv = heci_send_msg_timestamp(heci_cros_ec_handle, (uint8_t *)&evt,
				     sizeof(evt), timestamp);
	/*
	 * heci_send_msg_timestamp sends back negative error codes. Change to
	 * EC style codes
	 */
	return rv < 0 ? -rv : EC_SUCCESS;
}

static void heci_send_hostcmd_response(struct host_packet *pkt)
{
	int rv;
	struct cros_ec_ishtp_msg *out =
		(struct cros_ec_ishtp_msg *)response_buffer;

	out->hdr.channel = CROS_EC_COMMAND;
	out->hdr.status = 0;
	/* id is already set in the receiving method */

	rv = heci_send_msg(heci_cros_ec_handle, (uint8_t *)out,
			   pkt->response_size + CROS_EC_ISHTP_MSG_HDR_SIZE);
	if (rv < 0)
		CPRINTS("HC response failed %d", -rv);
}

static void cros_ec_ishtp_subsys_new_msg_received(const heci_handle_t handle,
					uint8_t *msg, const size_t msg_size)
{
	struct cros_ec_ishtp_msg *in = (void *) msg;
	struct cros_ec_ishtp_msg *out = (void *) response_buffer;

	if (in->hdr.channel != CROS_EC_COMMAND) {
		CPRINTS("Unknown HECI packet 0x%02x", in->hdr.channel);
		return;
	}
	memset(&heci_packet, 0, sizeof(heci_packet));

	/* Copy over id from sender so they can pair up messages */
	out->hdr.id = in->hdr.id;

	heci_packet.send_response = heci_send_hostcmd_response;

	heci_packet.request = in->data;
	heci_packet.request_max = HECI_MAX_MSG_SIZE;
	heci_packet.request_size = msg_size - CROS_EC_ISHTP_MSG_HDR_SIZE;

	heci_packet.response = out->data;
	heci_packet.response_max = HECI_CROS_EC_RESPONSE_MAX;
	heci_packet.response_size = 0;

	heci_packet.driver_result = EC_RES_SUCCESS;
	host_packet_receive(&heci_packet);
}

/*
 * IPC transfer max is actual 4K, but we don't need kernel buffers that big
 *
 * Basing size off of existing cros_ec implementations ranging from 128 to 512
 */
#define HECI_CROS_EC_LIMIT_PACKET_SIZE 256

/**
 * Get protocol information
 */
static enum ec_status heci_get_protocol_info(struct host_cmd_handler_args *args)
{
	struct ec_response_get_protocol_info *r = args->response;

	memset(r, 0, sizeof(*r));
	r->protocol_versions = BIT(3);
	r->max_request_packet_size = HECI_CROS_EC_LIMIT_PACKET_SIZE;
	r->max_response_packet_size = HECI_CROS_EC_RESPONSE_MAX;

	args->response_size = sizeof(*r);

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PROTOCOL_INFO, heci_get_protocol_info,
EC_VER_MASK(0));

static int cros_ec_ishtp_subsys_initialize(const heci_handle_t heci_handle)
{
	heci_cros_ec_handle = heci_handle;
	ish_fwst_set_fw_status(FWSTS_SENSOR_APP_RUNNING);
	return EC_SUCCESS;
}

static int cros_ec_ishtp_no_op(const heci_handle_t heci_handle)
{
	return EC_SUCCESS;
}

static void cros_ec_ishtp_disconneted(const heci_handle_t heci_handle)
{
}

static const struct heci_client_callbacks cros_ec_ishtp_subsys_heci_cbs = {
	.initialize = cros_ec_ishtp_subsys_initialize,
	.new_msg_received = cros_ec_ishtp_subsys_new_msg_received,
	.suspend = cros_ec_ishtp_no_op,
	.resume = cros_ec_ishtp_no_op,
	.disconnected = cros_ec_ishtp_disconneted,
};

static const struct heci_client cros_ec_ishtp_heci_client = {
	.protocol_id = HECI_CLIENT_CROS_EC_ISH_GUID,
	.max_msg_size = HECI_MAX_MSG_SIZE,
	.protocol_ver = 1,
	.max_n_of_connections = 1,
	.cbs = &cros_ec_ishtp_subsys_heci_cbs,
};

HECI_CLIENT_ENTRY(cros_ec_ishtp_heci_client);
