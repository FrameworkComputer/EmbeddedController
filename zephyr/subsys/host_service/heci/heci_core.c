/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "../host_service_common.h"
#include "bsp_helper.h"
#include "heci_dma.h"
#include "heci_internal.h"
#include "heci_system_state.h"
#include "host_bsp_service.h"

#include <stdlib.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(heci, CONFIG_HECI_LOG_LEVEL);

#define MAX_SERVICE_CLIENTS 16

struct heci_device_t heci_dev;
K_MUTEX_DEFINE(dev_lock);
struct k_sem flow_ctrl_sems[HECI_MAX_NUM_OF_CONNECTIONS];

struct heci_bsp_t *host_intf;
static bsp_msg_handler_f protocol_cb[MAX_SERVICE_CLIENTS] = { 0 };

static heci_msg_proc_handler_f proc_hdls[HECI_FIXED_CLIENT_NUM];

static inline void heci_lock(void)
{
	k_mutex_lock(&dev_lock, K_FOREVER);
}

static inline void heci_unlock(void)
{
	k_mutex_unlock(&dev_lock);
}

static int send_bsp_msg(uint32_t drbl, uint8_t *msg, uint32_t len)
{
	if (len > HECI_PACKET_SIZE) {
		LOG_ERR("Illegal size %d", len);
	}

	LOG_HEXDUMP_DBG((uint8_t *)msg, len, "heci outcoming");
	LOG_DBG("drbl = %08x", drbl);
	if (host_intf->send_msg == NULL) {
		return -1;
	}
	return host_intf->send_msg(drbl, msg, len);
}

bool heci_send_proto_msg(uint8_t host_addr, uint8_t fw_addr, bool last_frag,
			 uint8_t *data, uint32_t len)
{
	__ASSERT(data != NULL, "invalid arg: *data\n");
	__ASSERT(len <= HECI_MAX_PAYLOAD_SIZE, "invalid payload size\n");

	int ret;
	uint32_t outbound_drbl;

	struct heci_bus_msg_t *msg =
		(struct heci_bus_msg_t *)heci_dev.send_buffer;

#ifdef CONFIG_RTD3
	ret = mng_host_access_req(HECI_HAL_DEFAULT_TIMEOUT);
	if (ret) {
		LOG_ERR("%s failed to request access to host %d", __func__,
			(uint32_t)ret);
		return false;
	}
#endif

	msg->hdr.host_addr = host_addr;
	msg->hdr.fw_addr = fw_addr;
	msg->hdr.last_frag = last_frag ? 1 : 0;
	msg->hdr.len = len;
	memcpy(msg->payload, data, len);
	len += sizeof(msg->hdr);

	outbound_drbl = BUILD_DRBL(len, PROTOCOL_HECI);
	ret = send_bsp_msg(outbound_drbl, heci_dev.send_buffer, len);
#ifdef CONFIG_RTD3
	mng_host_access_dereq();
#endif
	if (ret) {
		LOG_ERR("write HECI protocol message err");
		return false;
	}

	return true;
}

static bool send_client_msg(struct heci_conn_t *conn, struct mrd_t *msg)
{
	__ASSERT(conn != NULL, "invalid heci connection\n");
	__ASSERT(msg != NULL, "invalid heci client msg to send\n");

	struct heci_bus_msg_t *bus_msg =
		(struct heci_bus_msg_t *)heci_dev.send_buffer;

	int ret;
	unsigned int fragment_size;
	unsigned int done_bytes = 0;
	uint32_t out_drbl, copy_size, max_frag_size;

	max_frag_size = host_intf->max_fragment_size - sizeof(bus_msg->hdr);
#ifdef CONFIG_RTD3
	ret = mng_host_access_req(HECI_HAL_DEFAULT_TIMEOUT);
	if (ret) {
		LOG_ERR("%s failed to request access to host %d", __func__,
			(uint32_t)ret);
		return false;
	}
#endif
	/* Initialize heci bus message header */
	bus_msg->hdr.host_addr = conn->host_addr;
	bus_msg->hdr.fw_addr = conn->fw_addr;
	bus_msg->hdr.reserved = 0;
	bus_msg->hdr.last_frag = 0;

	while (msg != NULL) {
		fragment_size = 0;
		/* try to copy as much as we can into current fragment */
		while ((fragment_size < max_frag_size) && (msg != NULL)) {
			copy_size = MIN(msg->len - done_bytes,
					max_frag_size - fragment_size);

			memcpy(bus_msg->payload + fragment_size,
			       (uint8_t *)msg->buf + done_bytes, copy_size);

			done_bytes += copy_size;
			fragment_size += copy_size;

			if (done_bytes == msg->len) {
				/* continue to next MRD in chain */
				msg = msg->next;
				done_bytes = 0;
			}
		}

		if (msg == NULL) {
			/* this is the last fragment */
			bus_msg->hdr.last_frag = 1;
		}

		bus_msg->hdr.len = fragment_size;
		fragment_size += sizeof(bus_msg->hdr);

		out_drbl = BUILD_DRBL(fragment_size, PROTOCOL_HECI);
		ret = send_bsp_msg(out_drbl, heci_dev.send_buffer,
				   fragment_size);
#ifdef CONFIG_RTD3
		mng_host_access_dereq();
#endif
		if (ret) {
			LOG_ERR("write HECI client msg err %d", ret);
			return false;
		}
	}
	return true;
}

/*
 * wait host to send flow control to unblock sending task
 * called with dev_lock locked
 */
static bool heci_wait_for_flow_control(struct heci_conn_t *conn)
{
	int ret = 0;

	while (true) {
		ret = k_sem_take(conn->flow_ctrl_sem,
				 K_MSEC(CONFIG_HECI_FC_WAIT_TIMEOUT));

		heci_lock();
		if (ret) {
			LOG_WRN("heci send timed out");
			conn->wait_thread_count--;
			heci_unlock();
			return false;
		}
		if (conn->host_buffers) {
			conn->wait_thread_count--;
			heci_unlock();
			return true;
		}
		heci_unlock();
	}
}

static inline void heci_wakeup_sender(struct heci_conn_t *conn,
				      uint8_t num_of_thread)
{
	for (int i = 0; i < num_of_thread; i++) {
		k_sem_give(conn->flow_ctrl_sem);
	}
}

/* calculate heci msg length to send, return minus for invalid sending*/
static int32_t cal_send_msg_len(uint32_t conn_id, struct mrd_t *msg)
{
	__ASSERT(conn_id < HECI_MAX_NUM_OF_CONNECTIONS,
		 "invalid heci connection\n");
	__ASSERT(msg != NULL, "invalid heci msg\n");

	uint32_t total_len = 0;
	uint32_t max_size;
	const struct mrd_t *m = msg;
	struct heci_conn_t *conn;

	heci_lock();

	conn = &heci_dev.connections[conn_id];
	if (!(conn->state & HECI_CONN_STATE_OPEN)) {
		LOG_ERR("bad connection id %d, state 0x%x", conn_id,
			conn->state);
		heci_unlock();
		return -1;
	}

	max_size = conn->client->properties.max_msg_size;
	heci_unlock();

	/* make sure total message length is less than client's max_msg_size */
	while (m != NULL) {
		if ((m->len == 0) || (m->buf == NULL)) {
			LOG_ERR("invalid mrd desc: %p, buf: %p len: %u", m,
				m->buf, m->len);
			return -1;
		}

		total_len += m->len;
		if (total_len > max_size) {
			LOG_ERR("too big msg length %u\n", total_len);
			return -1;
		}
		m = m->next;
	}
	return total_len;
}

bool heci_send(uint32_t conn_id, struct mrd_t *msg)
{
	int32_t total_len;
	bool sent = false;

	total_len = cal_send_msg_len(conn_id, msg);

	if (total_len < 0) {
		return false;
	}
	heci_lock();

	struct heci_conn_t *conn = &heci_dev.connections[conn_id];

	LOG_DBG("heci send message to connection: %d(%d<->%d)", conn_id,
		conn->host_addr, conn->fw_addr);

	if (conn->host_buffers == 0) {
		LOG_DBG("wait for flow control\n");
		conn->wait_thread_count++;
		heci_unlock();
		heci_wait_for_flow_control(conn);
		heci_lock();
	}

#ifdef CONFIG_HECI_USE_DMA
	if ((total_len > CONFIG_HECI_DMA_THRESHOLD) &&
	    (conn->client->properties.dma_enabled)) {
		/* TODO: add dma support */
	}
#endif
	if (!sent) {
		sent = send_client_msg(conn, msg);
	}

	if (sent) {
		/* decrease FC credit */
		conn->host_buffers--;
	} else {
		LOG_ERR("heci send fail!");
	}

	heci_unlock();
	return sent;
}

bool heci_send_flow_control(uint32_t conn_id)
{
	bool ret;
	struct heci_conn_t *conn;
	struct heci_flow_ctrl_t fc;

	if (conn_id >= HECI_MAX_NUM_OF_CONNECTIONS) {
		LOG_ERR("bad conn id %u, can't send FC", conn_id);
		return false;
	}

	heci_lock();

	conn = &heci_dev.connections[conn_id];
	if (!(conn->state & HECI_CONN_STATE_OPEN)) {
		LOG_WRN("heci connection %d is closed now, fails to send fc",
			(int)conn_id);
		ret = false;
		goto out_unlock;
	}

	/* free connection rx buffer */
	if (conn->rx_buffer != NULL) {
		struct heci_rx_msg_t *buf = conn->rx_buffer;

		buf->length = 0;
		buf->type = 0;
		buf->msg_lock = MSG_UNLOCKED;
		conn->rx_buffer = NULL;
	}

	/* build flow control message */
	fc.command = HECI_BUS_MSG_FLOW_CONTROL;
	fc.host_addr = conn->host_addr;
	fc.fw_addr = conn->fw_addr;
	fc.number_of_packets = 1;
	fc.reserved = 0;

	LOG_DBG("to connection: %d(%d<->%d)", conn_id, conn->host_addr,
		conn->fw_addr);

	ret = heci_send_proto_msg(HECI_DRIVER_ADDRESS, HECI_DRIVER_ADDRESS,
				  true, (uint8_t *)&fc, sizeof(fc));

out_unlock:
	heci_unlock();
	return ret;
}

static inline void heci_notify_client(struct heci_conn_t *conn, uint32_t event)
{
	if (conn->event_cb) {
		LOG_DBG("event is: %d", event);
		conn->event_cb(event, conn->event_cb_arg);
	}
}

static struct heci_conn_t *heci_find_conn(uint8_t fw_addr, uint8_t host_addr,
					  uint8_t state)
{
	int i;
	struct heci_conn_t *conn = &heci_dev.connections[0];

	for (i = 0; i < HECI_MAX_NUM_OF_CONNECTIONS; i++, conn++) {
		if ((conn->state & state) && (conn->fw_addr == fw_addr) &&
		    (conn->host_addr == host_addr)) {
			return conn;
		}
	}

	return NULL;
}

static void heci_connection_reset(struct heci_conn_t *conn)
{
	if ((conn == NULL) || (conn->client == NULL)) {
		return;
	}

	conn->state = HECI_CONN_STATE_DISCONNECTING;
	heci_notify_client(conn, HECI_EVENT_DISCONN);
}

static void heci_version_resp(struct heci_bus_msg_t *msg)
{
	struct heci_version_req_t *req =
		(struct heci_version_req_t *)msg->payload;
	struct heci_version_resp_t resp = { 0 };

	LOG_DBG("");

	if (msg->hdr.len != sizeof(struct heci_version_req_t)) {
		LOG_ERR("wrong VERSION_REQ len %d", msg->hdr.len);
		return;
	}

	resp.command = HECI_BUS_MSG_VERSION_RESP;
	resp.major_ver = HECI_DRIVER_MAJOR_VERSION;
	resp.minor_ver = HECI_DRIVER_MINOR_VERSION;

	if ((req->major_ver == HECI_DRIVER_MAJOR_VERSION) &&
	    (req->minor_ver == HECI_DRIVER_MINOR_VERSION)) {
		resp.supported = 1;
	} else {
		resp.supported = 0;
	}

	heci_send_proto_msg(HECI_DRIVER_ADDRESS, HECI_DRIVER_ADDRESS, true,
			    (uint8_t *)&resp, sizeof(resp));
}

static void heci_stop_resp(struct heci_bus_msg_t *msg)
{
	struct heci_version_resp_t resp = { 0 };

	LOG_DBG("");

	heci_reset();

	resp.command = HECI_BUS_MSG_HOST_STOP_RESP;

	heci_send_proto_msg(HECI_DRIVER_ADDRESS, HECI_DRIVER_ADDRESS, true,
			    (uint8_t *)&resp, sizeof(resp));
}

static void heci_enum_resp(struct heci_bus_msg_t *msg)
{
	int i;
	struct heci_client_ctrl_t *client;
	struct heci_host_enum_req_t *req =
		(struct heci_host_enum_req_t *)msg->payload;
	struct heci_host_enum_resp_t resp = { 0 };

	LOG_DBG("");

	if (msg->hdr.len != sizeof(struct heci_host_enum_req_t)) {
		LOG_ERR("wrong ENUM_REQ len %d", msg->hdr.len);
		return;
	}

	client = heci_dev.clients;
	for (i = 0; i < heci_dev.registered_clients; i++, client++) {
		resp.valid_addresses[client->client_addr / BITS_PER_DW] |=
			1 << (client->client_addr % BITS_PER_DW);
		client->active = true;
	}
	resp.command = HECI_BUS_MSG_HOST_ENUM_RESP;

	heci_send_proto_msg(HECI_DRIVER_ADDRESS, HECI_DRIVER_ADDRESS, true,
			    (uint8_t *)&resp, sizeof(resp));

	/*
	 * Setting client_req_bits will allow host to be notified
	 * about new clients
	 */
	if (req->client_req_bits) {
		heci_dev.notify_new_clients = true;
	}
}

static void heci_client_prop(struct heci_bus_msg_t *msg)
{
	int i;
	struct heci_client_prop_req_t *req =
		(struct heci_client_prop_req_t *)msg->payload;
	struct heci_client_prop_resp_t resp = { 0 };

	LOG_DBG("");

	if (msg->hdr.len != sizeof(struct heci_client_prop_req_t)) {
		LOG_ERR("wrong PROP_REQ len %d", msg->hdr.len);
		return;
	}

	for (i = 0; i < HECI_MAX_NUM_OF_CLIENTS; i++) {
		if (heci_dev.clients[i].client_addr == req->address) {
			break;
		}
	}

	resp.command = HECI_BUS_MSG_HOST_CLIENT_PROP_RESP;
	resp.address = req->address;

	if (i == HECI_MAX_NUM_OF_CLIENTS) {
		resp.status = HECI_CONNECT_STATUS_CLIENT_NOT_FOUND;
	} else {
		struct heci_client_t *client = &heci_dev.clients[i].properties;

		resp.protocol_id = client->protocol_id;
		resp.protocol_ver = client->protocol_ver;
		resp.max_n_of_conns = client->max_n_of_connections;
		resp.max_msg_size = client->max_msg_size;

		resp.dma_header_length = client->dma_header_length;
		resp.dma_enabled = client->dma_enabled;

		resp.status = HECI_CONNECT_STATUS_SUCCESS;
	}

	heci_send_proto_msg(HECI_DRIVER_ADDRESS, HECI_DRIVER_ADDRESS, true,
			    (uint8_t *)&resp, sizeof(resp));
}

static struct heci_rx_msg_t *
heci_get_buffer_from_pool(struct heci_client_ctrl_t *client)
{
	struct heci_rx_msg_t *msg;

	if (client == NULL) {
		LOG_ERR("invalid client");
		return NULL;
	}

	msg = client->properties.rx_msg;
	if (msg->msg_lock == MSG_LOCKED) {
		LOG_ERR("client %d no free buf", client->client_addr);
		return NULL;
	}

	msg->msg_lock = MSG_LOCKED;
	return msg;
}

static void heci_connect_host(struct heci_bus_msg_t *msg)
{
	int client_id;
	int conn_id;
	struct heci_conn_req_t *req = (struct heci_conn_req_t *)msg->payload;
	struct heci_conn_resp_t resp = { 0 };
	struct heci_conn_t *idle_conn = NULL;
	struct heci_client_ctrl_t *client;

	if (msg->hdr.len != sizeof(struct heci_conn_req_t)) {
		LOG_ERR("wrong CONN_REQ len %d", msg->hdr.len);
		return;
	}

	resp.command = HECI_BUS_MSG_CLIENT_CONNECT_RESP;
	resp.fw_addr = req->fw_addr;
	resp.host_addr = req->host_addr;

	/* Try to find the client */
	for (client_id = 0; client_id < HECI_MAX_NUM_OF_CLIENTS; client_id++) {
		client = &heci_dev.clients[client_id];
		if (client->client_addr == req->fw_addr) {
			break;
		}
	}

	if (client_id == HECI_MAX_NUM_OF_CLIENTS) {
		LOG_ERR("conn-client %d not found", req->fw_addr);
		resp.status = HECI_CONNECT_STATUS_CLIENT_NOT_FOUND;
		goto out;
	}

	if (req->host_addr == 0) {
		LOG_ERR("client %d get an invalid host addr 0x%02x", client_id,
			req->host_addr);
		resp.status = HECI_CONNECT_STATUS_REJECTED;
		goto out;
	}

	/*
	 * check whether it's a dynamic client that the host didn't
	 * acknowledge with HECI_BUS_MSG_ADD_CLIENT_RESP message
	 */
	if (!client->active) {
		LOG_ERR("client %d is inactive", req->fw_addr);
		resp.status = HECI_CONNECT_STATUS_INACTIVE_CLIENT;
		goto out;
	}

	if (client->n_of_conns == client->properties.max_n_of_connections) {
		LOG_ERR("client %d exceeds max connection", client_id);
		resp.status = HECI_CONNECT_STATUS_REJECTED;
		goto out;
	}

	/* Look-up among existing dynamic address connections
	 * in order to validate the request
	 */
	for (conn_id = 0; conn_id < HECI_MAX_NUM_OF_CONNECTIONS; conn_id++) {
		struct heci_conn_t *conn = &heci_dev.connections[conn_id];

		if (conn->state == HECI_CONN_STATE_UNUSED) {
			idle_conn = conn;
			idle_conn->connection_id = conn_id;
			break;
		}
	}

	if (!idle_conn) {
		LOG_ERR("no free connection");
		resp.status = HECI_CONNECT_STATUS_REJECTED;
		goto out;
	}

	/*
	 * every connection saves its current rx buffer in order to
	 * free it after the client will read the content
	 */
	idle_conn->rx_buffer = heci_get_buffer_from_pool(client);
	if (idle_conn->rx_buffer == NULL) {
		LOG_ERR("no buffer allocated for client %d", client_id);
		resp.status = HECI_CONNECT_STATUS_REJECTED;
		goto out;
	}

	client->n_of_conns++;
	idle_conn->client = client;
	idle_conn->wait_thread_count = 0;
	idle_conn->host_addr = req->host_addr;
	idle_conn->fw_addr = req->fw_addr;
	idle_conn->state = HECI_CONN_STATE_OPEN;
	idle_conn->flow_ctrl_sem = &(flow_ctrl_sems[conn_id]);
	idle_conn->event_cb = client->properties.event_cb;
	idle_conn->event_cb_arg = client->properties.event_cb_arg;

	k_sem_init(idle_conn->flow_ctrl_sem, 0, UINT_MAX);

	/* send connection handle to client */
	idle_conn->rx_buffer->type = HECI_CONNECT;
	idle_conn->rx_buffer->connection_id = idle_conn->connection_id;
	idle_conn->rx_buffer->length = 0;
	heci_notify_client(idle_conn, HECI_EVENT_NEW_MSG);

	/* send response to host */
	resp.status = HECI_CONNECT_STATUS_SUCCESS;

	LOG_DBG("client connect to host conn=%d(%d<->%d)",
		idle_conn->connection_id, req->host_addr, req->fw_addr);
out:
	heci_send_proto_msg(HECI_DRIVER_ADDRESS, HECI_DRIVER_ADDRESS, true,
			    (uint8_t *)&resp, sizeof(resp));
}

static void heci_disconnect(struct heci_bus_msg_t *msg)
{
	struct heci_conn_t *conn;
	struct heci_disconn_req_t *req =
		(struct heci_disconn_req_t *)msg->payload;

	if (msg->hdr.len != sizeof(struct heci_disconn_req_t)) {
		LOG_ERR("wrong DISCONN_REQ len %d", msg->hdr.len);
		return;
	}

	/* Look-up for a connection in either HECI_CONN_STATE_OPEN state
	 * or HECI_CONN_STATE_CONNECTION_REQUEST state
	 */
	conn = heci_find_conn(req->fw_addr, req->host_addr,
			      HECI_CONN_STATE_OPEN |
				      HECI_CONN_STATE_CONNECTION_REQUEST);
	if (conn != NULL) {
		LOG_DBG("disconnect req from host, conn: %d(%d<->%d)",
			conn->connection_id, req->host_addr, req->fw_addr);

		if (conn->state & HECI_CONN_STATE_DISCONNECTING) {
			/*
			 * The connection is already in process of
			 * disconnecting, no need to signal to client
			 */
			conn->state = HECI_CONN_STATE_DISCONNECTING |
				      HECI_CONN_STATE_SEND_DISCONNECT_RESP;
		} else {
			conn->state = HECI_CONN_STATE_DISCONNECTING |
				      HECI_CONN_STATE_SEND_DISCONNECT_RESP;
			heci_notify_client(conn, HECI_EVENT_DISCONN);
		}
	} else {
		LOG_ERR("invalid disconn req-host_addr = %d fw_addr = %d",
			req->host_addr, req->fw_addr);

		/* send a disconnect response to host */
		struct heci_disconn_resp_t resp = { 0 };

		resp.command = HECI_BUS_MSG_CLIENT_DISCONNECT_RESP;
		resp.host_addr = req->host_addr;
		resp.fw_addr = req->fw_addr;
		resp.status = HECI_CONNECT_STATUS_CLIENT_NOT_FOUND;

		heci_send_proto_msg(HECI_DRIVER_ADDRESS, HECI_DRIVER_ADDRESS,
				    true, (uint8_t *)&resp, sizeof(resp));
	}
}

static void heci_flow_control_recv(struct heci_bus_msg_t *msg)
{
	struct heci_conn_t *conn;
	struct heci_flow_ctrl_t *flowctrl =
		(struct heci_flow_ctrl_t *)msg->payload;

	if (msg->hdr.len != sizeof(struct heci_flow_ctrl_t)) {
		LOG_ERR("wrong FLOW_CTRL len %d", msg->hdr.len);
		return;
	}

	conn = heci_find_conn(flowctrl->fw_addr, flowctrl->host_addr,
			      HECI_CONN_STATE_OPEN);
	if (conn) {
		if (flowctrl->number_of_packets == 0) {
			conn->host_buffers++;
			heci_wakeup_sender(conn,
					   MIN(1, conn->wait_thread_count));
		} else {
			conn->host_buffers += flowctrl->number_of_packets;
			heci_wakeup_sender(conn,
					   MIN(flowctrl->number_of_packets,
					       conn->wait_thread_count));
		}

		LOG_DBG(" conn:%d(%d<->%d)", conn->connection_id,
			conn->host_addr, conn->fw_addr);
	} else {
		LOG_ERR("no valid connection");
	}
}

static void heci_reset_resp(struct heci_bus_msg_t *msg)
{
	struct heci_conn_t *conn;
	struct heci_reset_req_t *req = (struct heci_reset_req_t *)msg->payload;
	struct heci_reset_resp_t resp = { 0 };

	LOG_DBG("");

	if (msg->hdr.len != sizeof(struct heci_reset_req_t)) {
		LOG_ERR("wrong RESET_REQ len %d", msg->hdr.len);
		return;
	}

	conn = heci_find_conn(req->fw_addr, req->host_addr,
			      HECI_CONN_STATE_OPEN);
	if (conn) {
		conn->host_buffers = 0;

		resp.command = HECI_BUS_MSG_RESET_RESP;
		resp.host_addr = req->host_addr;
		resp.fw_addr = req->fw_addr;
		resp.status = HECI_CONNECT_STATUS_SUCCESS;
		heci_send_proto_msg(HECI_DRIVER_ADDRESS, HECI_DRIVER_ADDRESS,
				    true, (uint8_t *)&resp, sizeof(resp));
	}

	/*
	 * Just ignore the message for non-existing connection or
	 * in an inappropriate state
	 */
}

static void heci_add_client_resp(struct heci_bus_msg_t *msg)
{
	int i;

	LOG_DBG("");
	struct heci_add_client_resp_t *resp =
		(struct heci_add_client_resp_t *)msg->payload;

	if (msg->hdr.len != sizeof(struct heci_add_client_resp_t)) {
		LOG_ERR("wrong ADD_CLIENT_RESP len %dn", msg->hdr.len);
		return;
	}

	if (resp->status != 0) {
		LOG_ERR("can't activate client %d resp status %d",
			resp->client_addr, resp->status);
		return;
	}

	for (i = 0; i < HECI_MAX_NUM_OF_CLIENTS; i++) {
		if (heci_dev.clients[i].client_addr == resp->client_addr) {
			heci_dev.clients[i].active = 1;
			LOG_DBG("client %d active", resp->client_addr);
			return;
		}
	}

	LOG_ERR("client %d not found", resp->client_addr);
}

static void heci_connection_error(struct heci_conn_t *conn)
{
	struct heci_disconn_req_t req;

	/* closing the connection and sending disconnect request to host */
	req.command = HECI_BUS_MSG_CLIENT_DISCONNECT_REQ;
	req.host_addr = conn->host_addr;
	req.fw_addr = conn->fw_addr;
	req.reserved = 0;

	heci_send_proto_msg(HECI_DRIVER_ADDRESS, HECI_DRIVER_ADDRESS, true,
			    (uint8_t *)&req, sizeof(req));
}

static struct heci_conn_t *heci_find_active_conn(uint8_t fw_addr,
						 uint8_t host_addr)
{
	struct heci_conn_t *conn;

	conn = heci_find_conn(fw_addr, host_addr,
			      HECI_CONN_STATE_OPEN |
				      HECI_CONN_STATE_PROCESSING_MSG);
	if (!conn) {
		LOG_ERR("did not find conn %u %u", fw_addr, host_addr);
		return NULL;
	}

	/* if it's the first fragment */
	if (!(conn->state & HECI_CONN_STATE_PROCESSING_MSG)) {
		conn->rx_buffer = heci_get_buffer_from_pool(conn->client);
		if (conn->rx_buffer == NULL) {
			LOG_ERR("connection buffer locked");
			return NULL;
		}

		conn->state |= HECI_CONN_STATE_PROCESSING_MSG;
		conn->rx_buffer->length = 0;
		conn->rx_buffer->type = HECI_REQUEST;
		conn->rx_buffer->connection_id = conn->connection_id;
	}

	return conn;
}

static void heci_copy_to_client_buf(struct heci_conn_t *conn, uint64_t src_addr,
				    int len, bool dma)
{
	struct heci_rx_msg_t *rxmsg = conn->rx_buffer;
	struct heci_client_ctrl_t *client = conn->client;
	uint8_t *dst = &rxmsg->buffer[rxmsg->length];

	/* check if bad packet */
	if ((rxmsg->length + len > client->properties.rx_buffer_len) ||
	    (rxmsg->length + len > client->properties.max_msg_size)) {
		LOG_ERR("invalid buffer len: %d curlen: %d", rxmsg->length,
			len);
		heci_connection_error(conn);
		rxmsg->msg_lock = MSG_UNLOCKED;
		conn->state &= ~HECI_CONN_STATE_PROCESSING_MSG;
		return;
	}

	if (!dma) {
		memcpy(dst, (void *)(uintptr_t)src_addr, len);
	} else {
		/* TODO : add dma support*/
	}
	rxmsg->length += len;

	rxmsg->type = HECI_REQUEST;
	rxmsg->connection_id = conn->connection_id;
}

__weak void heci_dma_alloc_notification(struct heci_bus_msg_t *msg)
{
	struct heci_bus_dma_alloc_resp_t resp = { 0 };
	int status = 0;

	/*response host*/
	resp.command = HECI_BUS_MSG_DMA_ALLOC_RESP;
	resp.status = status;

	heci_send_proto_msg(HECI_DRIVER_ADDRESS, HECI_DRIVER_ADDRESS, true,
			    (uint8_t *)&resp, sizeof(resp));
}

static void heci_process_bus_message(struct heci_bus_msg_t *msg)
{
	uint8_t cmd = msg->payload[0];

	LOG_DBG("cmd:%d", cmd);
	switch (cmd) {
	case HECI_BUS_MSG_VERSION_REQ:
		heci_version_resp(msg);
		break;
	case HECI_BUS_MSG_HOST_STOP_REQ:
		heci_stop_resp(msg);
		break;
	case HECI_BUS_MSG_HOST_ENUM_REQ:
		heci_enum_resp(msg);
		break;
	case HECI_BUS_MSG_HOST_CLIENT_PROP_REQ:
		heci_client_prop(msg);
		break;
	case HECI_BUS_MSG_CLIENT_CONNECT_REQ:
		heci_connect_host(msg);
		break;
	case HECI_BUS_MSG_CLIENT_DISCONNECT_REQ:
		heci_disconnect(msg);
		break;
	case HECI_BUS_MSG_FLOW_CONTROL:
		heci_flow_control_recv(msg);
		break;
	case HECI_BUS_MSG_RESET_REQ:
		heci_reset_resp(msg);
		break;
	case HECI_BUS_MSG_ADD_CLIENT_RESP:
		heci_add_client_resp(msg);
		break;
	case HECI_BUS_MSG_DMA_ALLOC_NOTIFY_REQ:
		heci_dma_alloc_notification(msg);
		break;
#if CONFIG_HECI_USE_DMA
	case HECI_BUS_MSG_DMA_XFER_REQ: /* DMA transfer HOST to FW */
		LOG_DBG("host got host dma data req");
		break;
	case HECI_BUS_MSG_DMA_XFER_RESP: /* Ack for DMA transfer from FW */
		LOG_DBG("host got fw dma data");
		heci_dma_xfer_ack(msg);
		break;
#endif
	/* should never get this */
	case HECI_BUS_MSG_CLIENT_DISCONNECT_RESP:
		LOG_ERR("receiving DISCONNECT_RESP message");
	default:
		break;
	}
}

static void heci_process_client_message(struct heci_bus_msg_t *msg)
{
	struct heci_conn_t *conn;

	conn = heci_find_active_conn(msg->hdr.fw_addr, msg->hdr.host_addr);
	if (!conn) {
		LOG_ERR(" no valid connection");
		return;
	}

	LOG_DBG("conn:%d(%d<->%d)", conn->connection_id, msg->hdr.host_addr,
		msg->hdr.fw_addr);
	heci_copy_to_client_buf(conn, (uintptr_t)msg->payload, msg->hdr.len,
				false);

	/* send msg to client */
	if (msg->hdr.last_frag) {
		conn->state &= ~HECI_CONN_STATE_PROCESSING_MSG;
		heci_notify_client(conn, HECI_EVENT_NEW_MSG);
	}
}

void heci_reset(void)
{
	int i;

	heci_lock();

	for (i = 0; i < HECI_MAX_NUM_OF_CONNECTIONS; i++) {
		struct heci_conn_t *conn = &heci_dev.connections[i];

		if (conn->state & HECI_CONN_STATE_OPEN) {
			heci_connection_reset(conn);
		} else if (conn->state & HECI_CONN_STATE_DISCONNECTING) {
			/* client was already signaled with disconnect event
			 * no need to signal again
			 */
			conn->state = HECI_CONN_STATE_DISCONNECTING;
		}
	}

	heci_unlock();
}

static bool heci_client_find(struct heci_client_t *client)
{
	int i;
	struct heci_client_ctrl_t *client_ctrl = heci_dev.clients;

	for (i = 0; i < HECI_MAX_NUM_OF_CLIENTS; i++, client_ctrl++) {
		if (memcmp(&client->protocol_id,
			   &client_ctrl->properties.protocol_id,
			   sizeof(client->protocol_id)) == 0) {
			return true;
		}
	}

	return false;
}

static void heci_send_new_client_msg(struct heci_client_ctrl_t *client)
{
	struct heci_add_client_req_t req = { 0 };

	if (!heci_dev.notify_new_clients) {
		return;
	}

#define ASSIGN_PROP(ID) (req.client_properties.ID = client->properties.ID)

	req.client_addr = client->client_addr;
	req.command = HECI_BUS_MSG_ADD_CLIENT_REQ;

	ASSIGN_PROP(protocol_id);
	ASSIGN_PROP(dma_enabled);
	ASSIGN_PROP(dma_header_length);
	ASSIGN_PROP(max_msg_size);
	ASSIGN_PROP(max_n_of_connections);
	ASSIGN_PROP(protocol_ver);
	req.client_properties.fixed_address = client->client_addr;

#undef ASSIGN_PROP

	LOG_DBG("");
	heci_send_proto_msg(HECI_DRIVER_ADDRESS, HECI_DRIVER_ADDRESS, true,
			    (uint8_t *)&req, sizeof(req));
}

int heci_complete_disconnect(uint32_t conn_id)
{
	int ret;
	struct heci_conn_t *conn;

	if (conn_id >= HECI_MAX_NUM_OF_CONNECTIONS) {
		LOG_ERR("bad conn id %u", conn_id);
		return -EINVAL;
	}

	heci_lock();

	conn = &heci_dev.connections[conn_id];
	if (!(conn->state & HECI_CONN_STATE_DISCONNECTING)) {
		LOG_ERR("disconn conn %u, state 0x%x", conn_id, conn->state);
		ret = -EINVAL;
		goto out_unlock;
	}

	LOG_DBG(" conn %u(%d<->%d)", conn_id, conn->host_addr, conn->fw_addr);

	/* clean connection rx buffer */
	if (conn->rx_buffer != NULL) {
		struct heci_rx_msg_t *buf = conn->rx_buffer;

		buf->type = 0;
		buf->length = 0;
		buf->connection_id = 0;
		buf->msg_lock = MSG_UNLOCKED;
	}

	if (conn->state & HECI_CONN_STATE_SEND_DISCONNECT_RESP) {
		/* send a disconnect response to host with the old host_addr */
		struct heci_disconn_resp_t resp = { 0 };

		resp.command = HECI_BUS_MSG_CLIENT_DISCONNECT_RESP;
		resp.host_addr = conn->host_addr;
		resp.fw_addr = conn->fw_addr;
		resp.status = HECI_CONNECT_STATUS_SUCCESS;

		heci_send_proto_msg(HECI_DRIVER_ADDRESS, HECI_DRIVER_ADDRESS,
				    true, (uint8_t *)&resp, sizeof(resp));
	}

	conn->client->n_of_conns--;
	memset(conn, 0, sizeof(*conn));

out_unlock:
	heci_unlock();
	return 0;
}

int heci_register(struct heci_client_t *client)
{
	int i;
	struct heci_client_ctrl_t *client_ctrl = heci_dev.clients;

	if ((client == NULL) || (client->rx_msg == NULL) ||
	    (client->rx_msg->buffer == NULL)) {
		LOG_ERR("can't register client for bad params");
		return -EINVAL;
	}

	if (client->max_msg_size > CONFIG_HECI_MAX_MSG_SIZE) {
		LOG_ERR("client msg size couldn't be larger than %d bytes",
			CONFIG_HECI_MAX_MSG_SIZE);
		return -EINVAL;
	}

	heci_lock();

	/* Check if can find the client on the list. */
	if (heci_client_find(client)) {
		LOG_ERR("client already registered");
		heci_unlock();
		return -EBUSY;
	}

	for (i = 0; i < HECI_MAX_NUM_OF_CLIENTS; i++, client_ctrl++) {
		if (client_ctrl->client_addr == 0) {
			break;
		}
	}

	if (i == HECI_MAX_NUM_OF_CLIENTS) {
		heci_unlock();
		LOG_ERR("heci client resource is used up, failed to register");
		return -1;
	}
	heci_dev.registered_clients++;
	client_ctrl->properties = *client;
	client_ctrl->client_addr = (uint16_t)(i + HECI_FIXED_CLIENT_NUM);
	client_ctrl->n_of_conns = 0;
	client_ctrl->active = false;
	heci_send_new_client_msg(client_ctrl);

	heci_unlock();
	LOG_DBG("client is registered successfully with client id = %d",
		client_ctrl->client_addr);
	return 0;
}

int heci_add_fix_clients(uint32_t addr, heci_msg_proc_handler_f hdl)
{
	if ((addr >= HECI_FIXED_CLIENT_NUM) || (addr == 0)) {
		return -EINVAL;
	}

	LOG_INF("fixed client added: addr:0x%02x", addr);
	proc_hdls[addr] = hdl;
	return 0;
}

static void heci_process_message(struct heci_bus_msg_t *msg)
{
	heci_lock();

	if (msg->hdr.fw_addr == 0) {
		/* addr 0 is for heci bus protocol */
		heci_process_bus_message(msg);
	} else if (msg->hdr.fw_addr >= HECI_FIXED_CLIENT_NUM) {
		/* addr >= 32 is for clients communication*/
		heci_process_client_message(msg);
	} else if (proc_hdls[msg->hdr.fw_addr]) {
		/*
		 * addr 1~31 is for fixed client communication,
		 * which doesn't need connections and flow control
		 */
		proc_hdls[msg->hdr.fw_addr]((void *)msg->payload, msg->hdr.len);
	} else if ((msg->hdr.fw_addr) == HECI_SYSTEM_STATE_CLIENT_ADDR) {
		heci_handle_system_state_msg(msg->payload, msg->hdr.len);
	} else {
		LOG_INF("no handler for addr 0x%08x", msg->hdr.fw_addr);
	}

	heci_unlock();
}

static inline void ack_host(void)
{
	host_intf->send_ack();
#ifdef CONFIG_SYS_MNG
	if (host_intf->mng_msg_support) {
		send_rx_complete();
	}
#endif
}

static int heci_handler(uint32_t drbl)
{
	struct heci_bus_msg_t *heci_msg =
		(struct heci_bus_msg_t *)heci_dev.read_buffer;

	int ret;
	int msg_len = HEADER_GET_LENGTH(drbl);

	if (msg_len > host_intf->max_fragment_size) {
		LOG_ERR("invalid heci msg len");
		ack_host();
		return -1;
	}

	ret = host_intf->read_msg(&drbl, (uint8_t *)heci_msg, msg_len);
	ack_host();
	if (ret) {
		LOG_ERR("err %d", ret);
		return -1;
	}

	LOG_HEXDUMP_DBG((uint8_t *)heci_msg, msg_len, "heci incoming");
	LOG_DBG("drbl %08x", drbl);

	/* judge if it is a valid heci msg*/
	if (heci_msg->hdr.len + sizeof(heci_msg->hdr) == msg_len) {
		heci_process_message(heci_msg);
		return 0;
	}

	LOG_ERR("invalid HECI msg");
	return -1;
}

int heci_init(struct device *arg)
{
	ARG_UNUSED(arg);
	int ret;

	LOG_DBG("heci started");

	ret = host_protocol_register(PROTOCOL_HECI, heci_handler);
	if (ret != 0) {
		LOG_ERR("fail to add heci_handler as cb fun");
	}
	__ASSERT(host_intf != NULL, "host interface not found for heci\n");
	return ret;
}

int host_protocol_register(uint8_t protocol_id, bsp_msg_handler_f handler)
{
	if ((handler == NULL) || (protocol_id >= MAX_SERVICE_CLIENTS)) {
		LOG_ERR("bad params");
		return -1;
	}

	if (protocol_cb[protocol_id] != NULL) {
		LOG_WRN("host protocol registered already");
		return -1;
	}
	protocol_cb[protocol_id] = handler;
	LOG_INF("add handler function, protocol_id=%d", protocol_id);
	return 0;
}

void process_host_msgs(void)
{
	uint32_t inbound_drbl = 0, protocol, core_id;
	int ret;

	ret = host_intf->read_msg(&inbound_drbl, NULL, 0);
	if ((ret != 0) || (inbound_drbl & BIT(DRBL_BUSY_OFFS)) == 0) {
		return;
	}
	protocol = HEADER_GET_PROTOCOL(inbound_drbl);
	core_id = HEADER_GET_COREID(inbound_drbl);

	LOG_DBG("drbl %08x", inbound_drbl);
	__ASSERT(protocol < MAX_SERVICE_CLIENTS, "bad protocol");

	if ((protocol_cb[protocol] == NULL) ||
	    (core_id != CONFIG_HECI_CORE_ID)) {
		LOG_ERR("no cb for  protocol id = %d coreid = %d", protocol,
			core_id);
		host_intf->send_ack();
	} else {
		protocol_cb[protocol](inbound_drbl);
	}
}
