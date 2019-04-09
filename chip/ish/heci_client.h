/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __HECI_CLIENT_H
#define __HECI_CLIENT_H

#include <stdint.h>
#include <stddef.h>

#include "hooks.h"

#define HECI_MAX_NUM_OF_CLIENTS			2

#define HECI_MAX_MSG_SIZE			4960
#define HECI_IPC_PAYLOAD_SIZE   (IPC_MAX_PAYLOAD_SIZE - 4)
#define HECI_MAX_MSGS				3

enum HECI_ERR {
	HECI_ERR_TOO_MANY_MSG_ITEMS		= EC_ERROR_INTERNAL_FIRST + 0,
	HECI_ERR_NO_CRED_FROM_CLIENT_IN_HOST	= EC_ERROR_INTERNAL_FIRST + 1,
	HECI_ERR_CLIENT_IS_NOT_CONNECTED	= EC_ERROR_INTERNAL_FIRST + 2,
};

typedef void *					heci_handle_t;

#define HECI_INVALID_HANDLE			NULL

struct heci_guid {
	uint32_t data1;
	uint16_t data2;
	uint16_t data3;
	uint8_t data4[8];
};

struct heci_client_callbacks {
	/*
	 * called while registering heci client.
	 * if returns non-zero, the registration will fail.
	 */
	int (*initialize)(const heci_handle_t handle);
	/* called when new heci msg for the client is arrived */
	void (*new_msg_received)(const heci_handle_t handle, uint8_t *msg,
				 const size_t msg_size);
	/* called when the heci client is disconnected */
	void (*disconnected)(const heci_handle_t handle);

	/* called when ISH goes to suspend */
	int (*suspend)(const heci_handle_t);
	/* called when ISH resumes */
	int (*resume)(const heci_handle_t);
};

struct heci_client {
	struct heci_guid protocol_id;
	uint32_t max_msg_size;
	uint8_t protocol_ver;
	uint8_t max_n_of_connections;
	uint8_t dma_header_length :7;
	uint8_t dma_enabled :1;

	const struct heci_client_callbacks *cbs;
};

struct heci_msg_item {
	size_t size;
	uint8_t *buf;
};

struct heci_msg_list {
	int num_of_items;
	struct heci_msg_item *items[HECI_MAX_MSGS];
};

/*
 * Do not call this function directly.
 * The function should be called only by HECI_CLIENT_ENTRY()
 */
heci_handle_t heci_register_client(const struct heci_client *client);
int heci_set_client_data(const heci_handle_t handle, void *data);
void *heci_get_client_data(const heci_handle_t handle);

/*
 * Send a client message. Note this function waits a short while for the HECI
 * bus to become available for sending. This method blocks until either the heci
 * message is sent or the message as been queued to send in the lower IPC layer.
 *
 * All callers that use the same underlying IPC channel will be serialized.
 */
int heci_send_msg(const heci_handle_t handle, uint8_t *buf,
		  const size_t buf_size);
int heci_send_msg_timestamp(const heci_handle_t handle, uint8_t *buf,
		  const size_t buf_size, uint32_t *timestamp);
/*
 * send client msgs(using list of buffer&size).
 * heci_msg_item with size == 0 is not acceptable.
 */
int heci_send_msgs(const heci_handle_t handle,
		   const struct heci_msg_list *msg_list);
/* send msg to fixed client(system level client) */
int heci_send_fixed_client_msg(const uint8_t fw_addr, uint8_t *buf,
			       const size_t buf_size);

#define HECI_CLIENT_ENTRY(heci_client) \
	void _heci_entry_##heci_client(void) \
	{ \
		heci_register_client(&(heci_client)); \
	} \
	DECLARE_HOOK(HOOK_INIT, _heci_entry_##heci_client, HOOK_PRIO_LAST - 1)

#endif /* __HECI_CLIENT_H */
