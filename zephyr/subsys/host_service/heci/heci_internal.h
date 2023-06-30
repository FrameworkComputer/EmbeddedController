/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _HECI_INTENAL_H_
#define _HECI_INTENAL_H_
#include "heci.h"
#include "heci_intf.h"

#include <stdint.h>

#include <zephyr/kernel.h>

#define BITS_PER_DW 32
#define HECI_HAL_DEFAULT_TIMEOUT 5000

#define HECI_PACKET_SIZE CONFIG_HECI_MAX_FRAG_SIZE
#define HECI_MAX_PAYLOAD_SIZE (HECI_PACKET_SIZE - sizeof(struct heci_hdr_t))
#define HECI_MIN_DMA_SIZE 512
#define PAGE_BITMAP_NUM 10
#define MAX_HOST_SIZE (PAGE_BITMAP_NUM * BITS_PER_DW * CONFIG_HECI_PAGE_SIZE)

#define HECI_FIXED_CLIENT_NUM 32
#define HECI_MAX_NUM_OF_CLIENTS 8
/* max numbers of heci connections, each client has one connection */
#define HECI_MAX_NUM_OF_CONNECTIONS HECI_MAX_NUM_OF_CLIENTS

#define HECI_DRIVER_MAJOR_VERSION 1
#define HECI_DRIVER_MINOR_VERSION 0

#define HECI_DRIVER_ADDRESS 0
#define HECI_SYSTEM_STATE_CLIENT_ADDR 13
#define HECI_FW_STATE_CLIENT_ADDR 14

#define GEN_RESP(cmd) (0x80 | (HECI_BUS_MSG_##cmd##_REQ))

#define HECI_BUS_MSG_VERSION_REQ 0x01
#define HECI_BUS_MSG_VERSION_RESP GEN_RESP(VERSION)
#define HECI_BUS_MSG_HOST_STOP_REQ 0x02
#define HECI_BUS_MSG_HOST_STOP_RESP GEN_RESP(HOST_STOP)
#define HECI_BUS_MSG_ME_STOP_REQ 0x03
#define HECI_BUS_MSG_HOST_ENUM_REQ 0x04
#define HECI_BUS_MSG_HOST_ENUM_RESP GEN_RESP(HOST_ENUM)
#define HECI_BUS_MSG_HOST_CLIENT_PROP_REQ 0x05
#define HECI_BUS_MSG_HOST_CLIENT_PROP_RESP GEN_RESP(HOST_CLIENT_PROP)
#define HECI_BUS_MSG_CLIENT_CONNECT_REQ 0x06
#define HECI_BUS_MSG_CLIENT_CONNECT_RESP GEN_RESP(CLIENT_CONNECT)
#define HECI_BUS_MSG_CLIENT_DISCONNECT_REQ 0x07
#define HECI_BUS_MSG_CLIENT_DISCONNECT_RESP GEN_RESP(CLIENT_DISCONNECT)
#define HECI_BUS_MSG_FLOW_CONTROL 0x08
#define HECI_BUS_MSG_RESET_REQ 0x09
#define HECI_BUS_MSG_RESET_RESP GEN_RESP(RESET)
#define HECI_BUS_MSG_ADD_CLIENT_REQ 0x0a
#define HECI_BUS_MSG_ADD_CLIENT_RESP GEN_RESP(ADD_CLIENT)
#define HECI_BUS_MSG_DMA_ALLOC_NOTIFY_REQ 0x11
#define HECI_BUS_MSG_DMA_ALLOC_RESP GEN_RESP(DMA_ALLOC_NOTIFY)
#define HECI_BUS_MSG_DMA_XFER_REQ 0x12
#define HECI_BUS_MSG_DMA_XFER_RESP GEN_RESP(DMA_XFER)

enum HECI_CONN_STATE {
	HECI_CONN_STATE_UNUSED = 0,
	HECI_CONN_STATE_OPEN = (1 << 0),
	HECI_CONN_STATE_PROCESSING_MSG = (1 << 1),
	HECI_CONN_STATE_DISCONNECTING = (1 << 2),
	HECI_CONN_STATE_CONNECTION_REQUEST = (1 << 3),
	HECI_CONN_STATE_SEND_DISCONNECT_RESP = (1 << 4),
};

enum HECI_BUS_MSG_STATUS {
	HECI_CONNECT_STATUS_SUCCESS = 0,
	HECI_CONNECT_STATUS_CLIENT_NOT_FOUND = 1,
	HECI_CONNECT_STATUS_ALREADY_EXISTS = 2,
	HECI_CONNECT_STATUS_REJECTED = 3,
	HECI_CONNECT_STATUS_INVALID_PARAMETER = 4,
	HECI_CONNECT_STATUS_INACTIVE_CLIENT = 5,
};

struct heci_version_req_t {
	uint8_t command;
	uint8_t reserved;
	uint8_t minor_ver;
	uint8_t major_ver;
} __packed;

struct heci_version_resp_t {
	uint8_t command;
	uint8_t supported;
	uint8_t minor_ver;
	uint8_t major_ver;
} __packed;

struct heci_host_enum_req_t {
	uint8_t command;
	uint8_t client_req_bits;
	uint8_t reserved[2];
} __packed;

struct heci_host_enum_resp_t {
	uint8_t command;
	uint8_t reserved[3];
	uint32_t valid_addresses[8];
} __packed;

struct heci_client_prop_req_t {
	uint8_t command;
	uint8_t address;
	uint8_t reserved[2];
} __packed;

struct heci_client_prop_resp_t {
	uint8_t command;
	uint8_t address;
	uint8_t status;
	uint8_t reserved_1;
	struct heci_guid_t protocol_id;
	uint8_t protocol_ver;
	uint8_t max_n_of_conns;
	uint8_t reserved_2;
	uint8_t reserved_3;
	uint32_t max_msg_size;
	uint8_t dma_header_length : 7;
	uint8_t dma_enabled : 1;
	uint8_t reserved_4[3];
} __packed;

struct heci_conn_req_t {
	uint8_t command;
	uint8_t fw_addr;
	uint8_t host_addr;
	uint8_t reserved;
} __packed;

struct heci_conn_resp_t {
	uint8_t command;
	uint8_t fw_addr;
	uint8_t host_addr;
	uint8_t status;
} __packed;

struct heci_disconn_req_t {
	uint8_t command;
	uint8_t fw_addr;
	uint8_t host_addr;
	uint8_t reserved;
} __packed;

struct heci_disconn_resp_t {
	uint8_t command;
	uint8_t fw_addr;
	uint8_t host_addr;
	uint8_t status;
} __packed;

struct heci_flow_ctrl_t {
	uint8_t command;
	uint8_t fw_addr;
	uint8_t host_addr;
	uint8_t number_of_packets;
	uint32_t reserved;
} __packed;

struct heci_reset_req_t {
	uint8_t command;
	uint8_t fw_addr;
	uint8_t host_addr;
	uint8_t reserved1;
} __packed;

struct heci_reset_resp_t {
	uint8_t command;
	uint8_t fw_addr;
	uint8_t host_addr;
	uint8_t status;
} __packed;

struct heci_client_properties_t {
	struct heci_guid_t protocol_id;
	uint8_t protocol_ver;
	uint8_t max_n_of_connections;
	uint8_t fixed_address;
	uint8_t single_receive_buffer;
	uint32_t max_msg_size;

	uint8_t dma_header_length : 7;
	uint8_t dma_enabled : 1;
	uint8_t reserved[3];
} __packed;

struct heci_add_client_req_t {
	uint8_t command;
	uint8_t client_addr;
	uint8_t reserved[2];
	struct heci_client_properties_t client_properties;
} __packed;

struct heci_add_client_resp_t {
	uint8_t command;
	uint8_t client_addr;
	uint8_t status;
} __packed;

struct dma_msg_info_t {
	/* address in host memory where message is located.
	 * Bits 0-11 must be 0
	 */
	uint64_t msg_addr_in_host;
	uint32_t msg_length;
	uint8_t reserved[4];
} __packed;

struct heci_bus_dma_xfer_resp_t {
	uint8_t command;
	uint8_t fw_addr;
	uint8_t host_addr;
	uint8_t reserved;
	struct dma_msg_info_t dma_buf[0];
} __packed;

struct heci_bus_dma_xfer_req_t {
	uint8_t command;
	uint8_t fw_addr;
	uint8_t host_addr;
	uint8_t reserved;
	/* address in host memory,  bits 0-11 must be 0 */
	uint64_t msg_addr_in_host;
	uint32_t msg_length;
	uint8_t reserved2[4];
} __packed;

struct dma_buf_info_t {
	uint32_t buf_size;
	/*
	 * The address in host memory for clients subsequent DMA messages.
	 * Bits 0-11 must be 0.
	 */
	uint64_t buf_address;
} __packed;

struct heci_bus_dma_alloc_notif_req_t {
	uint8_t command;
	uint8_t reserved[3];
	struct dma_buf_info_t alloc_dma_buf[0];
} __packed;

struct heci_bus_dma_alloc_resp_t {
	uint8_t command;
	uint8_t status; /* 0 = success */
	uint8_t reserved[2];
} __packed;

struct heci_hdr_t {
	uint32_t fw_addr : 8;
	uint32_t host_addr : 8;
	uint32_t len : 12;
	uint32_t reserved : 2;
	uint32_t secure : 1;
	uint32_t last_frag : 1;
} __packed;

struct heci_bus_msg_t {
	struct heci_hdr_t hdr;
	uint8_t payload[0];
} __packed;

struct heci_client_ctrl_t {
	uint8_t client_addr;
	uint8_t n_of_conns;
	bool active;
	struct heci_client_t properties;
};

struct heci_conn_t {
	struct heci_client_ctrl_t *client;
	uint8_t state;
	uint8_t fw_addr;
	uint8_t host_addr;
	uint8_t wait_thread_count;
	/*
	 * every connection saves its current rx buffer in order to
	 * free after the client reads the content.
	 */
	struct heci_rx_msg_t *rx_buffer;
	heci_event_cb_t event_cb;
	void *event_cb_arg;

	uint32_t host_buffers;
	void *flow_ctrl_sem;
	uint8_t host_dram_addr[8];
	uint32_t dma_buff_size;
	uint32_t dma_ts; /* DMA timestamp */
	uint8_t connection_id;
};

struct host_rx_dma_info {
	uint64_t dma_addr;
	uint32_t size;
	uint32_t num_pages;
	atomic_t page_bitmap[BITS_PER_DW];
	struct k_sem dma_sem;
	struct k_mutex dma_lock;
};

struct heci_device_t {
	struct heci_client_ctrl_t clients[HECI_MAX_NUM_OF_CLIENTS];
	struct heci_conn_t connections[HECI_MAX_NUM_OF_CONNECTIONS];
	uint8_t read_buffer[HECI_PACKET_SIZE];
	/* reserved for drbl, when continuous memory is required for
	 * drbl and send buffer
	 */
	uint32_t reserved[1];
	uint8_t send_buffer[HECI_PACKET_SIZE];

	bool dma_req;
	bool notify_new_clients;
	int registered_clients;

	/* used to send large buffer to host by DMA */
	struct host_rx_dma_info host_rx_dma;
};

bool heci_send_proto_msg(uint8_t host_addr, uint8_t fw_addr, bool last_frag,
			 uint8_t *data, uint32_t len);

void process_host_msgs(void);

#endif /* _HECI_INTENAL_H_ */
