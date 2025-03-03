/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "host_command.h"
#include "ipi_chip.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_IPI, format, ##args)
#define CPRINTS(format, args...) cprints(CC_IPI, format, ##args)

#define HOSTCMD_MAX_REQUEST_SIZE CONFIG_IPC_SHARED_OBJ_BUF_SIZE
/* Reserve 1 extra byte for HOSTCMD_TYPE and 3 bytes for padding. */
#define HOSTCMD_MAX_RESPONSE_SIZE (CONFIG_IPC_SHARED_OBJ_BUF_SIZE - 4)
#define HOSTCMD_TYPE_HOSTCMD 1
#define HOSTCMD_TYPE_HOSTEVENT 2

/*
 * hostcmd and hostevent share the same IPI ID, and use first byte type to
 * indicate its type.
 */
static struct hostcmd_data {
	const uint8_t type;
	/* To be compatible with CONFIG_HOSTCMD_ALIGNED */
	uint8_t response[HOSTCMD_MAX_RESPONSE_SIZE] __aligned(4);
} hc_cmd_obj = { .type = HOSTCMD_TYPE_HOSTCMD };
BUILD_ASSERT(sizeof(struct hostcmd_data) == CONFIG_IPC_SHARED_OBJ_BUF_SIZE);

/* Size of the rpmsg device name, should sync across kernel and EC. */
#define RPMSG_NAME_SIZE 32

/*
 * The layout of name service message.
 * This should sync across kernel and EC.
 */
struct rpmsg_ns_msg {
	/* Name of the corresponding rpmsg_driver. */
	char name[RPMSG_NAME_SIZE];
	/* IPC ID */
	uint32_t addr;
};

static void hostcmd_send_response_packet(struct host_packet *pkt)
{
	int ret;

	ret = ipi_send(SCP_IPI_HOST_COMMAND, &hc_cmd_obj,
		       pkt->response_size +
			       offsetof(struct hostcmd_data, response),
		       1);
	if (ret)
		CPRINTS("failed to %s(), ret=%d", __func__, ret);
}

static void hostcmd_handler(int32_t id, void *buf, uint32_t len)
{
	static struct host_packet packet;
	uint8_t *in_msg = buf;
	struct ec_host_request *r = (struct ec_host_request *)in_msg;
	int i;

	if (in_msg[0] != EC_HOST_REQUEST_VERSION) {
		CPRINTS("ERROR: Protocol V2 is not supported!");
		CPRINTF("in_msg=[");
		for (i = 0; i < len; i++)
			CPRINTF("%02x ", in_msg[i]);
		CPRINTF("]\n");
		return;
	}

	/* Protocol version 3 */

	packet.send_response = hostcmd_send_response_packet;

	/*
	 * Just assign the buffer to request, host_packet_receive
	 * handles the buffer copy.
	 */
	packet.request = (void *)r;
	packet.request_temp = NULL;
	packet.request_max = HOSTCMD_MAX_REQUEST_SIZE;
	packet.request_size = host_request_expected_size(r);

	packet.response = hc_cmd_obj.response;
	/* Reserve space for the preamble and trailing byte */
	packet.response_max = HOSTCMD_MAX_RESPONSE_SIZE;
	packet.response_size = 0;

	packet.driver_result = EC_RES_SUCCESS;

	host_packet_receive(&packet);
}
DECLARE_IPI(SCP_IPI_HOST_COMMAND, hostcmd_handler, 0);

/*
 * Get protocol information
 */
static enum ec_status
hostcmd_get_protocol_info(struct host_cmd_handler_args *args)
{
	struct ec_response_get_protocol_info *r = args->response;

	memset(r, 0, sizeof(*r));
	r->protocol_versions |= BIT(3);
	r->max_request_packet_size = HOSTCMD_MAX_REQUEST_SIZE;
	r->max_response_packet_size = HOSTCMD_MAX_RESPONSE_SIZE;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PROTOCOL_INFO, hostcmd_get_protocol_info,
		     EC_VER_MASK(0));

void hostcmd_init(void)
{
	int ret;
	struct rpmsg_ns_msg ns_msg;

	if (IS_ENABLED(CONFIG_RPMSG_NAME_SERVICE)) {
		ns_msg.addr = SCP_IPI_HOST_COMMAND;
		strncpy(ns_msg.name, "cros-ec-rpmsg", RPMSG_NAME_SIZE);
		ret = ipi_send(SCP_IPI_NS_SERVICE, &ns_msg, sizeof(ns_msg), 1);
		if (ret)
			CPRINTS("Failed to announce host command channel");
	}
}
