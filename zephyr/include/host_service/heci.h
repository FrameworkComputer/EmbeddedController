/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _HECI_H_
#define _HECI_H_
/**
 * @brief HECI Interface
 * @defgroup heci_interface HECI Interface
 * @{
 */

#include "zephyr/kernel.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief memory resource descriptor
 *
 * This structure defines descriptor indicating the HECI client message to send.
 */
struct mrd_t {
	struct mrd_t *next;
	void const *buf;
	uint32_t len;
};

/* HECI received message types*/
typedef uint8_t heci_rx_msg_type;
enum MSG_TYPE {
	HECI_MSG_BASE,
	HECI_REQUEST = HECI_MSG_BASE,
	HECI_CONNECT,
	HECI_DISCONNECT,
	HECI_RX_DMA_MSG,
	HECI_SYNC_RESP,
	HECI_MSG_LAST,
};

enum heci_bsp_id_t { HECI_BSP_ID_IPC, HECI_BSP_ID_IPINTF, HECI_BSP_ID_MAX };

#define MSG_LOCKED 1
#define MSG_UNLOCKED 0

/* HECI received message format and bit map */
struct heci_rx_msg_t {
	/* message type of this message */
	heci_rx_msg_type type;
	/* connection id bewteen HOST and local clients*/
	uint8_t connection_id : 7;
	/* if HOST client has enough buffer*/
	uint8_t msg_lock : 1;
	/* HECI message buffer length */
	uint16_t length;
	/* buffer pointer */
	uint8_t *buffer;
};

/* HECI GUID format, protocol id*/
struct heci_guid_t {
	unsigned long data1;
	unsigned short data2;
	unsigned short data3;
	unsigned char data4[8];
};

/* HECI event indicating there is a new message */
#define HECI_EVENT_NEW_MSG (1 << 0)
/* HECI event indicating HOST wants to disonnect client */
#define HECI_EVENT_DISCONN (1 << 1)

/*
 * @brief
 * callback to handle certain events
 */
typedef void (*heci_event_cb_t)(uint32_t, void *);

struct heci_client_t {
	/* A 16-byte identifier for the protocol supported by the client.*/
	struct heci_guid_t protocol_id;
	uint32_t max_msg_size;
	uint8_t protocol_ver;
	uint8_t max_n_of_connections; /* only support single connection */
	uint8_t dma_header_length : 7;
	uint8_t dma_enabled : 1;
	enum heci_bsp_id_t bsp;
	/* allocated buffer len of rx_msg->buffer */
	uint32_t rx_buffer_len;
	struct heci_rx_msg_t *rx_msg;
	heci_event_cb_t event_cb;
	void *event_cb_arg;
};

/*
 * @brief
 * register a new HECI client
 * @param client Pointer to the client structure for the instance.
 * @retval 0 If successful.
 */
int heci_register(struct heci_client_t *client);

/*
 * @brief
 * send HECI message to HOST client with certain connection
 * @param conn_id connection id for sending.
 * @param msg message content pointer to send.
 * @retval true If successful.
 */
bool heci_send(uint32_t conn_id, struct mrd_t *msg);

/*
 * @brief
 * send HECI flow control message to HOST client, indicating that local client
 * is ready for receiving a new HECI message
 * @param conn_id connection id for sending.
 * @retval true If successful.
 */
bool heci_send_flow_control(uint32_t conn_id);

/*
 * @brief
 * complete disconnection between local client and HOST client, run after
 * receiving a disconnection request from HOST client
 * @param conn_id connection id to disconnect.
 * @retval 0 If successful.
 */
int heci_complete_disconnect(uint32_t conn_id);

void get_clock_sync_data(uint64_t *last_fw_clock, uint64_t *last_host_clock_utc,
			 uint64_t *last_host_clock_system);

/*
 * @brief
 * message handler callback to proceed the heci message
 * @param msg message pointer for received heci buffer
 * @param len message length for received heci buffer
 */
typedef void (*heci_msg_proc_handler_f)(void *msg, uint32_t len);

/*
 * @brief
 * add fix client message proceed handler for fixed function client
 * @param addr fixed client addr
 * @param hdl message proceed function
 * @retval 0 If successful.
 */
int heci_add_fix_clients(uint32_t addr, heci_msg_proc_handler_f hdl);

#ifdef __cplusplus
}
#endif
/*
 * @}
 */
#endif /* _HECI_H_ */
