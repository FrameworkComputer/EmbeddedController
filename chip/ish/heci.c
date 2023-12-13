/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "builtin/assert.h"
#include "compile_time_macros.h"
#include "console.h"
#include "hbm.h"
#include "heci_client.h"
#include "ipc_heci.h"
#include "system_state.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTS(format, args...) cprints(CC_LPC, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_LPC, format, ##args)

struct heci_header {
	uint8_t fw_addr;
	uint8_t host_addr;
	uint16_t length; /* [8:0] length, [14:9] reserved, [15] msg_complete */
} __packed;
#define HECI_MSG_CMPL_SHIFT 15
#define HECI_MSG_LENGTH_MASK 0x01FF
#define HECI_MSG_LENGTH(length) ((length) & HECI_MSG_LENGTH_MASK)
#define HECI_MSG_IS_COMPLETED(length) \
	(!!((length) & (0x01 << HECI_MSG_CMPL_SHIFT)))

BUILD_ASSERT(HECI_IPC_PAYLOAD_SIZE ==
	     (IPC_MAX_PAYLOAD_SIZE - sizeof(struct heci_header)));

struct heci_msg {
	struct heci_header hdr;
	uint8_t payload[HECI_IPC_PAYLOAD_SIZE];
} __packed;

/* HECI addresses */
#define HECI_HBM_ADDRESS 0 /* HECI Bus Message */
#define HECI_DYN_CLIENT_ADDR_START 0x20 /* Dynamic client start addr */

/* A fw client has the same value for both handle and fw address */
#define TO_FW_ADDR(handle) ((uintptr_t)(handle))
#define TO_HECI_HANDLE(fw_addr) ((heci_handle_t)(uintptr_t)(fw_addr))
/* convert client fw address to client context index */
#define TO_CLIENT_CTX_IDX(fw_addr) ((fw_addr)-HECI_DYN_CLIENT_ADDR_START)

/* should be less than HECI_INVALID_HANDLE - 1 */
BUILD_ASSERT(HECI_MAX_NUM_OF_CLIENTS < 0x0FE);

struct heci_client_connect {
	uint8_t is_connected; /* client is connected to host */
	uint8_t host_addr; /* connected host address */

	/* receiving message */
	uint8_t ignore_rx_msg;
	uint8_t rx_msg[HECI_MAX_MSG_SIZE];
	size_t rx_msg_length;

	uint32_t flow_ctrl_creds; /* flow control */
	struct mutex lock; /* protects against 2 writers */
	struct mutex cred_lock; /* protects flow ctrl */
	int waiting_task;
};

struct heci_client_context {
	const struct heci_client *client;
	void *data; /* client specific data */

	struct heci_client_connect connect; /* connection context */
	struct ss_subsys_device ss_device; /* system state receiver device */
};

struct heci_bus_context {
	ipc_handle_t ipc_handle; /* ipc handle for heci protocol */

	int num_of_clients;
	struct heci_client_context client_ctxs[HECI_MAX_NUM_OF_CLIENTS];
};

/* declare heci bus */
struct heci_bus_context heci_bus_ctx = {
	.ipc_handle = IPC_INVALID_HANDLE,
};

static inline struct heci_client_context *
heci_get_client_context(const uint8_t fw_addr)
{
	return &heci_bus_ctx.client_ctxs[TO_CLIENT_CTX_IDX(fw_addr)];
}

static inline struct heci_client_connect *
heci_get_client_connect(const uint8_t fw_addr)
{
	struct heci_client_context *cli_ctx = heci_get_client_context(fw_addr);
	return &cli_ctx->connect;
}

static inline int heci_is_client_connected(const uint8_t fw_addr)
{
	struct heci_client_context *cli_ctx = heci_get_client_context(fw_addr);
	return cli_ctx->connect.is_connected;
}

static inline int heci_is_valid_client_addr(const uint8_t fw_addr)
{
	uint8_t cli_idx = TO_CLIENT_CTX_IDX(fw_addr);

	return cli_idx < heci_bus_ctx.num_of_clients;
}

static inline int heci_is_valid_handle(const heci_handle_t handle)
{
	return heci_is_valid_client_addr((uintptr_t)(handle));
}

/* find heci device that contains this system state device in it */
#define ss_device_to_heci_client_context(ss_dev) \
	((struct heci_client_context             \
		  *)((void *)(ss_dev) -          \
		     (void *)(&(                 \
			     ((struct heci_client_context *)0)->ss_device))))
#define client_context_to_handle(cli_ctx)                                       \
	((heci_handle_t)((uint32_t)((cli_ctx) - &heci_bus_ctx.client_ctxs[0]) / \
				 sizeof(heci_bus_ctx.client_ctxs[0]) +          \
			 1))

/*
 * each heci device registered as system state device which gets
 * system state(e.g. suspend/resume, portrait/landscape) events
 * through system state subsystem from host
 */
static int heci_client_suspend(struct ss_subsys_device *ss_device)
{
	struct heci_client_context *cli_ctx =
		ss_device_to_heci_client_context(ss_device);
	heci_handle_t handle = client_context_to_handle(cli_ctx);

	if (cli_ctx->client->cbs->suspend)
		cli_ctx->client->cbs->suspend(handle);

	return EC_SUCCESS;
}

static int heci_client_resume(struct ss_subsys_device *ss_device)
{
	struct heci_client_context *cli_ctx =
		ss_device_to_heci_client_context(ss_device);
	heci_handle_t handle = client_context_to_handle(cli_ctx);

	if (cli_ctx->client->cbs->resume)
		cli_ctx->client->cbs->resume(handle);

	return EC_SUCCESS;
}

struct system_state_callbacks heci_ss_cbs = {
	.suspend = heci_client_suspend,
	.resume = heci_client_resume,
};

/*
 * This function should be called only by HECI_CLIENT_ENTRY()
 */
heci_handle_t heci_register_client(const struct heci_client *client)
{
	int ret;
	heci_handle_t handle;
	struct heci_client_context *cli_ctx;

	if (client == NULL || client->cbs == NULL)
		return HECI_INVALID_HANDLE;

	/*
	 * we don't need mutex here since this function is called by
	 * entry function which is serialized among heci clients.
	 */
	if (heci_bus_ctx.num_of_clients >= HECI_MAX_NUM_OF_CLIENTS)
		return HECI_INVALID_HANDLE;

	/* we only support 1 connection */
	if (client->max_n_of_connections > 1)
		return HECI_INVALID_HANDLE;

	if (client->max_msg_size > HECI_MAX_MSG_SIZE)
		return HECI_INVALID_HANDLE;

	/* create handle with the same value of fw address */
	handle = (heci_handle_t)(heci_bus_ctx.num_of_clients +
				 HECI_DYN_CLIENT_ADDR_START);
	cli_ctx = &heci_bus_ctx.client_ctxs[heci_bus_ctx.num_of_clients++];
	cli_ctx->client = client;

	if (client->cbs->initialize) {
		ret = client->cbs->initialize(handle);
		if (ret) {
			heci_bus_ctx.num_of_clients--;
			return HECI_INVALID_HANDLE;
		}
	}

	if (client->cbs->suspend || client->cbs->resume) {
		cli_ctx->ss_device.cbs = &heci_ss_cbs;
		ss_subsys_register_client(&cli_ctx->ss_device);
	}

	return handle;
}

static void heci_build_hbm_header(struct heci_header *hdr, uint32_t length)
{
	hdr->fw_addr = HECI_HBM_ADDRESS;
	hdr->host_addr = HECI_HBM_ADDRESS;
	hdr->length = length;
	/* payload of hbm is less than IPC payload */
	hdr->length |= (uint16_t)1 << HECI_MSG_CMPL_SHIFT;
}

static void heci_build_fixed_client_header(struct heci_header *hdr,
					   const uint8_t fw_addr,
					   const uint32_t length)
{
	hdr->fw_addr = fw_addr;
	hdr->host_addr = 0;
	hdr->length = length;
	/* Fixed client payload < IPC payload */
	hdr->length |= (uint16_t)1 << HECI_MSG_CMPL_SHIFT;
}

static int heci_send_heci_msg_timestamp(struct heci_msg *msg,
					uint32_t *timestamp)
{
	int length, written;

	if (heci_bus_ctx.ipc_handle == IPC_INVALID_HANDLE)
		return -1;

	length = sizeof(msg->hdr) + HECI_MSG_LENGTH(msg->hdr.length);
	written = ipc_write_timestamp(heci_bus_ctx.ipc_handle, msg, length,
				      timestamp);

	if (written != length) {
		CPRINTF("%s error : len = %d err = %d\n", __func__, (int)length,
			written);
		return -EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

static int heci_send_heci_msg(struct heci_msg *msg)
{
	return heci_send_heci_msg_timestamp(msg, NULL);
}

int heci_set_client_data(const heci_handle_t handle, void *data)
{
	struct heci_client_context *cli_ctx;
	const uint8_t fw_addr = TO_FW_ADDR(handle);

	if (!heci_is_valid_handle(handle))
		return -EC_ERROR_INVAL;

	cli_ctx = heci_get_client_context(fw_addr);
	cli_ctx->data = data;

	return EC_SUCCESS;
}

void *heci_get_client_data(const heci_handle_t handle)
{
	struct heci_client_context *cli_ctx;
	const uint8_t fw_addr = TO_FW_ADDR(handle);

	if (!heci_is_valid_handle(handle))
		return NULL;

	cli_ctx = heci_get_client_context(fw_addr);
	return cli_ctx->data;
}

/*
 * Waits for flow control credit that allows TX transactions
 *
 * Returns true if credit was acquired, otherwise false
 */
static int wait_for_flow_ctrl_cred(struct heci_client_connect *connect)
{
	int need_to_wait;

	do {
		mutex_lock(&connect->cred_lock);
		need_to_wait = !connect->flow_ctrl_creds;
		if (need_to_wait) {
			connect->waiting_task = task_get_current();
		} else {
			connect->flow_ctrl_creds = 0;
			connect->waiting_task = 0;
		}
		mutex_unlock(&connect->cred_lock);
		if (need_to_wait) {
			/*
			 * A second is more than enough, otherwise if will
			 * probably never happen.
			 */
			int ev = task_wait_event_mask(TASK_EVENT_IPC_READY,
						      SECOND);
			if (ev & TASK_EVENT_TIMER) {
				/* Return false, not able to get credit */
				return 0;
			}
		}
	} while (need_to_wait);

	/* We successfully got flow control credit */
	return 1;
}

int heci_send_msg_timestamp(const heci_handle_t handle, uint8_t *buf,
			    const size_t buf_size, uint32_t *timestamp)
{
	int buf_offset = 0, ret = 0, remain, payload_size;
	struct heci_client_connect *connect;
	struct heci_msg msg;
	const uint8_t fw_addr = TO_FW_ADDR(handle);

	if (!heci_is_valid_handle(handle))
		return -EC_ERROR_INVAL;

	if (buf_size > HECI_MAX_MSG_SIZE)
		return -EC_ERROR_OVERFLOW;

	connect = heci_get_client_connect(fw_addr);
	mutex_lock(&connect->lock);

	if (!heci_is_client_connected(fw_addr)) {
		ret = -HECI_ERR_CLIENT_IS_NOT_CONNECTED;
		goto err_locked;
	}

	if (!wait_for_flow_ctrl_cred(connect)) {
		CPRINTF("no cred\n");
		ret = -HECI_ERR_NO_CRED_FROM_CLIENT_IN_HOST;
		goto err_locked;
	}

	msg.hdr.fw_addr = fw_addr;
	msg.hdr.host_addr = connect->host_addr;

	remain = buf_size;
	while (remain) {
		if (remain > HECI_IPC_PAYLOAD_SIZE) {
			msg.hdr.length = HECI_IPC_PAYLOAD_SIZE;
			payload_size = HECI_IPC_PAYLOAD_SIZE;
		} else {
			msg.hdr.length = remain;
			/* set as last heci msg */
			msg.hdr.length |= (uint16_t)1 << HECI_MSG_CMPL_SHIFT;
			payload_size = remain;
		}

		memcpy(msg.payload, buf + buf_offset, payload_size);

		heci_send_heci_msg_timestamp(&msg, timestamp);

		remain -= payload_size;
		buf_offset += payload_size;
	}
	mutex_unlock(&connect->lock);

	return buf_size;

err_locked:
	mutex_unlock(&connect->lock);

	return ret;
}

int heci_send_msg(const heci_handle_t handle, uint8_t *buf,
		  const size_t buf_size)
{
	return heci_send_msg_timestamp(handle, buf, buf_size, NULL);
}

int heci_send_msgs(const heci_handle_t handle,
		   const struct heci_msg_list *msg_list)
{
	struct heci_msg_item *cur_item;
	int total_size = 0;
	int i, msg_cur_pos, buf_size, copy_size, msg_sent;
	struct heci_client_connect *connect;
	struct heci_msg msg;
	const uint8_t fw_addr = TO_FW_ADDR(handle);

	if (!heci_is_valid_handle(handle))
		return -EC_ERROR_INVAL;

	for (i = 0; i < msg_list->num_of_items; i++) {
		if (!msg_list->items[i]->size || !msg_list->items[i]->buf)
			return -EC_ERROR_INVAL;

		total_size += msg_list->items[i]->size;
	}

	if (total_size > HECI_MAX_MSG_SIZE)
		return -EC_ERROR_OVERFLOW;

	if (msg_list->num_of_items > HECI_MAX_MSGS)
		return -HECI_ERR_TOO_MANY_MSG_ITEMS;

	connect = heci_get_client_connect(fw_addr);
	mutex_lock(&connect->lock);

	if (!heci_is_client_connected(fw_addr)) {
		total_size = -HECI_ERR_CLIENT_IS_NOT_CONNECTED;
		goto err_locked;
	}

	if (!wait_for_flow_ctrl_cred(connect)) {
		CPRINTF("no cred\n");
		total_size = -HECI_ERR_NO_CRED_FROM_CLIENT_IN_HOST;
		goto err_locked;
	}

	msg.hdr.fw_addr = fw_addr;
	msg.hdr.host_addr = connect->host_addr;

	i = 1;
	msg_cur_pos = 0;
	buf_size = 0;
	cur_item = msg_list->items[0];
	msg_sent = 0;
	while (1) {
		/* get next item if current item is consumed */
		if (msg_cur_pos == cur_item->size) {
			/*
			 * break if no more item.
			 * if "msg" contains data to be sent
			 * it will be sent after break.
			 */
			if (i == msg_list->num_of_items)
				break;

			/* get next item and reset msg_cur_pos */
			cur_item = msg_list->items[i++];
			msg_cur_pos = 0;
		}

		/* send data in ipc buf if it's completely filled */
		if (buf_size == HECI_IPC_PAYLOAD_SIZE) {
			msg.hdr.length = buf_size;
			msg_sent += buf_size;

			/* no leftovers, send the last msg here */
			if (msg_sent == total_size) {
				msg.hdr.length |= (uint16_t)1
						  << HECI_MSG_CMPL_SHIFT;
			}

			heci_send_heci_msg(&msg);
			buf_size = 0;
		}

		/* fill ipc msg buffer */
		if (cur_item->size - msg_cur_pos >
		    HECI_IPC_PAYLOAD_SIZE - buf_size) {
			copy_size = HECI_IPC_PAYLOAD_SIZE - buf_size;
		} else {
			copy_size = cur_item->size - msg_cur_pos;
		}

		memcpy(msg.payload + buf_size, cur_item->buf + msg_cur_pos,
		       copy_size);

		msg_cur_pos += copy_size;
		buf_size += copy_size;
	}

	/* leftovers ? send last msg */
	if (buf_size != 0) {
		msg.hdr.length = buf_size;
		msg.hdr.length |= (uint16_t)1 << HECI_MSG_CMPL_SHIFT;

		heci_send_heci_msg(&msg);
	}

err_locked:
	mutex_unlock(&connect->lock);

	return total_size;
}

/* For now, we only support fixed client payload size < IPC payload size */
int heci_send_fixed_client_msg(const uint8_t fw_addr, uint8_t *buf,
			       const size_t buf_size)
{
	struct heci_msg msg;

	heci_build_fixed_client_header(&msg.hdr, fw_addr, buf_size);

	memcpy(msg.payload, buf, buf_size);

	heci_send_heci_msg(&msg);

	return EC_SUCCESS;
}

static int handle_version_req(struct hbm_version_req *ver_req)
{
	struct hbm_version_res *ver_res;
	struct heci_msg heci_msg;
	struct hbm_i2h *i2h;

	heci_build_hbm_header(&heci_msg.hdr,
			      sizeof(i2h->cmd) + sizeof(*ver_res));

	i2h = (struct hbm_i2h *)heci_msg.payload;
	i2h->cmd = HECI_BUS_MSG_VERSION_RESP;
	ver_res = (struct hbm_version_res *)&i2h->data;

	memset(ver_res, 0, sizeof(*ver_res));

	ver_res->version.major = HBM_MAJOR_VERSION;
	ver_res->version.minor = HBM_MINOR_VERSION;
	if (ver_req->version.major == HBM_MAJOR_VERSION &&
	    ver_req->version.minor == HBM_MINOR_VERSION) {
		ver_res->supported = 1;
	} else {
		ver_res->supported = 0;
	}

	heci_send_heci_msg(&heci_msg);

	return EC_SUCCESS;
}

#define BITS_PER_BYTE 8
/* get number of bits for one element of "valid_addresses" array */
#define BITS_PER_ELEMENT \
	(sizeof(((struct hbm_enum_res *)0)->valid_addresses[0]) * BITS_PER_BYTE)

static int handle_enum_req(struct hbm_enum_req *enum_req)
{
	struct hbm_enum_res *enum_res;
	struct heci_msg heci_msg;
	struct hbm_i2h *i2h;
	int i;

	heci_build_hbm_header(&heci_msg.hdr,
			      sizeof(i2h->cmd) + sizeof(*enum_res));

	i2h = (struct hbm_i2h *)heci_msg.payload;
	i2h->cmd = HECI_BUS_MSG_HOST_ENUM_RESP;
	enum_res = (struct hbm_enum_res *)&i2h->data;

	memset(enum_res, 0, sizeof(*enum_res));

	/*
	 * fw address 0 is reserved for HECI Bus Message
	 * fw address 1 ~ 0x1f are reserved for fixed clients
	 * fw address 0x20 ~ 0xFF is for dynamic clients
	 * bit-0 set -> fw address "0", bit-1 set -> fw address "1"
	 */
	for (i = HECI_DYN_CLIENT_ADDR_START;
	     i < heci_bus_ctx.num_of_clients + HECI_DYN_CLIENT_ADDR_START;
	     i++) {
		enum_res->valid_addresses[i / BITS_PER_ELEMENT] |=
			1 << (i & (BITS_PER_ELEMENT - 1));
	}

	heci_send_heci_msg(&heci_msg);

	return EC_SUCCESS;
}

static int handle_client_prop_req(struct hbm_client_prop_req *client_prop_req)
{
	struct hbm_client_prop_res *client_prop_res;
	struct heci_msg heci_msg;
	struct hbm_i2h *i2h;
	struct heci_client_context *client_ctx;
	const struct heci_client *client;

	heci_build_hbm_header(&heci_msg.hdr,
			      sizeof(i2h->cmd) + sizeof(*client_prop_res));

	i2h = (struct hbm_i2h *)heci_msg.payload;
	i2h->cmd = HECI_BUS_MSG_HOST_CLIENT_PROP_RESP;
	client_prop_res = (struct hbm_client_prop_res *)&i2h->data;

	memset(client_prop_res, 0, sizeof(*client_prop_res));

	client_prop_res->address = client_prop_req->address;
	if (!heci_is_valid_client_addr(client_prop_req->address)) {
		client_prop_res->status = HECI_CONNECT_STATUS_CLIENT_NOT_FOUND;
	} else {
		struct hbm_client_properties *client_prop;

		client_ctx = heci_get_client_context(client_prop_req->address);
		client = client_ctx->client;
		client_prop = &client_prop_res->client_prop;

		client_prop->protocol_name = client->protocol_id;
		client_prop->protocol_version = client->protocol_ver;
		client_prop->max_number_of_connections =
			client->max_n_of_connections;
		client_prop->max_msg_length = client->max_msg_size;
		client_prop->dma_hdr_len = client->dma_header_length;
		client_prop->dma_hdr_len |=
			client->dma_enabled ? CLIENT_DMA_ENABLE : 0;
	}

	heci_send_heci_msg(&heci_msg);

	return EC_SUCCESS;
}

static int heci_send_flow_control(uint8_t fw_addr)
{
	struct heci_client_connect *connect;
	struct hbm_i2h *i2h;
	struct hbm_flow_control *flow_ctrl;
	struct heci_msg heci_msg;

	connect = heci_get_client_connect(fw_addr);

	heci_build_hbm_header(&heci_msg.hdr,
			      sizeof(i2h->cmd) + sizeof(*flow_ctrl));

	i2h = (struct hbm_i2h *)heci_msg.payload;
	i2h->cmd = HECI_BUS_MSG_FLOW_CONTROL;
	flow_ctrl = (struct hbm_flow_control *)&i2h->data;

	memset(flow_ctrl, 0, sizeof(*flow_ctrl));

	flow_ctrl->fw_addr = fw_addr;
	flow_ctrl->host_addr = connect->host_addr;

	heci_send_heci_msg(&heci_msg);

	return EC_SUCCESS;
}

static int
handle_client_connect_req(struct hbm_client_connect_req *client_connect_req)
{
	struct hbm_client_connect_res *client_connect_res;
	struct heci_msg heci_msg;
	struct hbm_i2h *i2h;
	struct heci_client_connect *connect;

	heci_build_hbm_header(&heci_msg.hdr,
			      sizeof(i2h->cmd) + sizeof(*client_connect_res));

	i2h = (struct hbm_i2h *)heci_msg.payload;
	i2h->cmd = HECI_BUS_MSG_CLIENT_CONNECT_RESP;
	client_connect_res = (struct hbm_client_connect_res *)&i2h->data;

	memset(client_connect_res, 0, sizeof(*client_connect_res));

	client_connect_res->fw_addr = client_connect_req->fw_addr;
	client_connect_res->host_addr = client_connect_req->host_addr;
	if (!heci_is_valid_client_addr(client_connect_req->fw_addr)) {
		client_connect_res->status =
			HECI_CONNECT_STATUS_CLIENT_NOT_FOUND;
	} else if (!client_connect_req->host_addr) {
		client_connect_res->status =
			HECI_CONNECT_STATUS_INVALID_PARAMETER;
	} else {
		connect = heci_get_client_connect(client_connect_req->fw_addr);
		if (connect->is_connected) {
			client_connect_res->status =
				HECI_CONNECT_STATUS_ALREADY_EXISTS;
		} else {
			connect->is_connected = 1;
			connect->host_addr = client_connect_req->host_addr;
		}
	}

	heci_send_heci_msg(&heci_msg);

	/* no error, send flow control */
	if (!client_connect_res->status)
		heci_send_flow_control(client_connect_req->fw_addr);

	return EC_SUCCESS;
}

static int handle_flow_control_cmd(struct hbm_flow_control *flow_ctrl)
{
	struct heci_client_connect *connect;
	int waiting_task;

	if (!heci_is_valid_client_addr(flow_ctrl->fw_addr))
		return -1;

	if (!heci_is_client_connected(flow_ctrl->fw_addr))
		return -1;

	connect = heci_get_client_connect(flow_ctrl->fw_addr);

	mutex_lock(&connect->cred_lock);
	connect->flow_ctrl_creds = 1;
	waiting_task = connect->waiting_task;
	mutex_unlock(&connect->cred_lock);

	if (waiting_task)
		task_set_event(waiting_task, TASK_EVENT_IPC_READY);

	return EC_SUCCESS;
}

static void heci_handle_client_msg(struct heci_msg *msg, size_t length)
{
	struct heci_client_context *cli_ctx;
	struct heci_client_connect *connect;
	const struct heci_client_callbacks *cbs;
	int payload_size;

	if (!heci_is_valid_client_addr(msg->hdr.fw_addr))
		return;

	if (!heci_is_client_connected(msg->hdr.fw_addr))
		return;

	cli_ctx = heci_get_client_context(msg->hdr.fw_addr);
	cbs = cli_ctx->client->cbs;
	connect = &cli_ctx->connect;

	payload_size = HECI_MSG_LENGTH(msg->hdr.length);
	if (connect->is_connected && msg->hdr.host_addr == connect->host_addr) {
		if (!connect->ignore_rx_msg &&
		    connect->rx_msg_length + payload_size > HECI_MAX_MSG_SIZE) {
			connect->ignore_rx_msg = 1; /* too big. discard */
		}

		if (!connect->ignore_rx_msg) {
			memcpy(connect->rx_msg + connect->rx_msg_length,
			       msg->payload, payload_size);

			connect->rx_msg_length += payload_size;
		}

		if (HECI_MSG_IS_COMPLETED(msg->hdr.length)) {
			if (!connect->ignore_rx_msg) {
				cbs->new_msg_received(
					TO_HECI_HANDLE(msg->hdr.fw_addr),
					connect->rx_msg,
					connect->rx_msg_length);
			}

			connect->rx_msg_length = 0;
			connect->ignore_rx_msg = 0;

			heci_send_flow_control(msg->hdr.fw_addr);
		}
	}
}

static int handle_client_disconnect_req(
	struct hbm_client_disconnect_req *client_disconnect_req)
{
	struct hbm_client_disconnect_res *client_disconnect_res;
	struct heci_msg heci_msg;
	struct hbm_i2h *i2h;
	struct heci_client_context *cli_ctx;
	struct heci_client_connect *connect;
	const struct heci_client_callbacks *cbs;
	uint8_t fw_addr, host_addr;

	CPRINTS("Got HECI disconnect request");

	heci_build_hbm_header(&heci_msg.hdr,
			      sizeof(i2h->cmd) +
				      sizeof(*client_disconnect_res));

	i2h = (struct hbm_i2h *)heci_msg.payload;
	i2h->cmd = HECI_BUS_MSG_CLIENT_DISCONNECT_RESP;
	client_disconnect_res = (struct hbm_client_disconnect_res *)&i2h->data;

	memset(client_disconnect_res, 0, sizeof(*client_disconnect_res));

	fw_addr = client_disconnect_req->fw_addr;
	host_addr = client_disconnect_req->host_addr;

	client_disconnect_res->fw_addr = fw_addr;
	client_disconnect_res->host_addr = host_addr;
	if (!heci_is_valid_client_addr(fw_addr) ||
	    !heci_is_client_connected(fw_addr)) {
		client_disconnect_res->status =
			HECI_CONNECT_STATUS_CLIENT_NOT_FOUND;
	} else {
		connect = heci_get_client_connect(fw_addr);
		if (connect->host_addr != host_addr) {
			client_disconnect_res->status =
				HECI_CONNECT_STATUS_INVALID_PARAMETER;
		} else {
			cli_ctx = heci_get_client_context(fw_addr);
			cbs = cli_ctx->client->cbs;
			mutex_lock(&connect->lock);
			if (connect->is_connected) {
				cbs->disconnected(TO_HECI_HANDLE(fw_addr));
				connect->is_connected = 0;
			}
			mutex_unlock(&connect->lock);
		}
	}

	heci_send_heci_msg(&heci_msg);

	return EC_SUCCESS;
}

/* host stops due to version mismatch */
static int handle_host_stop_req(struct hbm_host_stop_req *host_stop_req)
{
	struct hbm_host_stop_res *host_stop_res;
	struct heci_msg heci_msg;
	struct hbm_i2h *i2h;

	heci_build_hbm_header(&heci_msg.hdr,
			      sizeof(i2h->cmd) + sizeof(*host_stop_res));

	i2h = (struct hbm_i2h *)heci_msg.payload;
	i2h->cmd = HECI_BUS_MSG_HOST_STOP_RESP;
	host_stop_res = (struct hbm_host_stop_res *)&i2h->data;

	memset(host_stop_res, 0, sizeof(*host_stop_res));

	heci_send_heci_msg(&heci_msg);

	return EC_SUCCESS;
}

static int is_hbm_validity(struct hbm_h2i *h2i, size_t length)
{
	int valid_msg_len;

	valid_msg_len = sizeof(h2i->cmd);

	switch (h2i->cmd) {
	case HECI_BUS_MSG_VERSION_REQ:
		valid_msg_len += sizeof(struct hbm_version_req);
		break;

	case HECI_BUS_MSG_HOST_ENUM_REQ:
		valid_msg_len += sizeof(struct hbm_enum_req);
		break;

	case HECI_BUS_MSG_HOST_CLIENT_PROP_REQ:
		valid_msg_len += sizeof(struct hbm_client_prop_req);
		break;

	case HECI_BUS_MSG_CLIENT_CONNECT_REQ:
		valid_msg_len += sizeof(struct hbm_client_connect_req);
		break;

	case HECI_BUS_MSG_FLOW_CONTROL:
		valid_msg_len += sizeof(struct hbm_flow_control);
		break;

	case HECI_BUS_MSG_CLIENT_DISCONNECT_REQ:
		valid_msg_len += sizeof(struct hbm_client_disconnect_req);
		break;

	case HECI_BUS_MSG_HOST_STOP_REQ:
		valid_msg_len += sizeof(struct hbm_host_stop_req);
		break;

/* TODO: DMA support for large data */
#if 0
	case HECI_BUS_MSG_DMA_REQ:
		valid_msg_len += sizeof(struct hbm_dma_req);
		break;

	case HECI_BUS_MSG_DMA_ALLOC_NOTIFY:
		valid_msg_len += sizeof(struct hbm_dma_alloc_notify);
		break;

	case HECI_BUS_MSG_DMA_XFER_REQ: /* DMA transfer to FW */
		valid_msg_len += sizeof(struct hbm_dma_xfer_req);
		break;

	case HECI_BUS_MSG_DMA_XFER_RESP: /* Ack for DMA transfer from FW */
		valid_msg_len += sizeof(struct hbm_dma_xfer_resp);
		break;
#endif
	default:
		break;
	}

	if (valid_msg_len != length) {
		CPRINTF("invalid cmd(%d) valid : %d, cur : %zd\n", h2i->cmd,
			valid_msg_len, length);
		/* TODO: invalid cmd. not sure to reply with error ? */
		return 0;
	}

	return 1;
}

static void heci_handle_hbm(struct hbm_h2i *h2i, size_t length)
{
	void *data = (void *)&h2i->data;

	if (!is_hbm_validity(h2i, length))
		return;

	switch (h2i->cmd) {
	case HECI_BUS_MSG_VERSION_REQ:
		handle_version_req((struct hbm_version_req *)data);
		break;

	case HECI_BUS_MSG_HOST_ENUM_REQ:
		handle_enum_req((struct hbm_enum_req *)data);
		break;

	case HECI_BUS_MSG_HOST_CLIENT_PROP_REQ:
		handle_client_prop_req((struct hbm_client_prop_req *)data);
		break;

	case HECI_BUS_MSG_CLIENT_CONNECT_REQ:
		handle_client_connect_req(
			(struct hbm_client_connect_req *)data);
		break;

	case HECI_BUS_MSG_FLOW_CONTROL:
		handle_flow_control_cmd((struct hbm_flow_control *)data);
		break;

	case HECI_BUS_MSG_CLIENT_DISCONNECT_REQ:
		handle_client_disconnect_req(
			(struct hbm_client_disconnect_req *)data);
		break;

	case HECI_BUS_MSG_HOST_STOP_REQ:
		handle_host_stop_req((struct hbm_host_stop_req *)data);
		break;

/* TODO: DMA transfer if data is too big >= ? KB */
#if 0
	case HECI_BUS_MSG_DMA_REQ:
		handle_dma_req((struct hbm_dma_req *)data);
		break;

	case HECI_BUS_MSG_DMA_ALLOC_NOTIFY:
		handle_dma_alloc_notify((struct hbm_dma_alloc_notify *));
		break;

	case HECI_BUS_MSG_DMA_XFER_REQ: /* DMA transfer to FW */
		handle_dma_xfer_req((struct hbm_dma_xfer_req *)data);
		break;

	case HECI_BUS_MSG_DMA_XFER_RESP: /* Ack for DMA transfer from FW */
		handle_dma_xfer_resp((struct hbm_dma_xfer_resp *)data);
		break;
#endif
	default:
		break;
	}
}

static void heci_handle_heci_msg(struct heci_msg *heci_msg, size_t msg_length)
{
	if (!heci_msg->hdr.host_addr) {
		/*
		 * message for HECI bus or a fixed client should fit
		 * into one IPC message
		 */
		if (!HECI_MSG_IS_COMPLETED(heci_msg->hdr.length)) {
			CPRINTS("message not completed");
			return;
		}

		if (heci_msg->hdr.fw_addr == HECI_FIXED_SYSTEM_STATE_ADDR)
			heci_handle_system_state_msg(
				heci_msg->payload,
				HECI_MSG_LENGTH(heci_msg->hdr.length));
		else if (!heci_msg->hdr.fw_addr)
			/* HECI Bus Message(fw_addr == 0 && host_addr == 0) */
			heci_handle_hbm((struct hbm_h2i *)heci_msg->payload,
					HECI_MSG_LENGTH(heci_msg->hdr.length));
		else
			CPRINTS("not supported fixed client(%d)",
				heci_msg->hdr.fw_addr);
	} else {
		/* host_addr != 0 : Msg for Dynamic client */
		heci_handle_client_msg(heci_msg, msg_length);
	}
}

/* event flag for HECI msg */
#define EVENT_FLAG_BIT_HECI_MSG TASK_EVENT_CUSTOM_BIT(0)

void heci_rx_task(void)
{
	int msg_len;
	struct heci_msg heci_msg;
	ipc_handle_t ipc_handle;

	/* open IPC for HECI protocol */
	heci_bus_ctx.ipc_handle = ipc_open(IPC_PEER_ID_HOST, IPC_PROTOCOL_HECI,
					   EVENT_FLAG_BIT_HECI_MSG);

	ASSERT(heci_bus_ctx.ipc_handle != IPC_INVALID_HANDLE);

	/* get ipc handle */
	ipc_handle = heci_bus_ctx.ipc_handle;

	while (1) {
		/* task will be blocked here, waiting for event */
		msg_len = ipc_read(ipc_handle, &heci_msg, sizeof(heci_msg), -1);

		if (msg_len <= 0) {
			CPRINTS("discard heci packet");
			continue;
		}

		if (HECI_MSG_LENGTH(heci_msg.hdr.length) +
			    sizeof(heci_msg.hdr) ==
		    msg_len)
			heci_handle_heci_msg(&heci_msg, msg_len);
		else
			CPRINTS("msg len mismatch.. discard..");
	}
}
