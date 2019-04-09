/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* IPC module for ISH */
#ifndef __IPC_HECI_H
#define __IPC_HECI_H

enum IPC_ERR {
	IPC_ERR_IPC_IS_NOT_READY	= EC_ERROR_INTERNAL_FIRST + 0,
	IPC_ERR_TOO_SMALL_BUFFER	= EC_ERROR_INTERNAL_FIRST + 1,
	IPC_ERR_TX_QUEUE_FULL		= EC_ERROR_INTERNAL_FIRST + 2,
	IPC_ERR_INVALID_TASK		= EC_ERROR_INTERNAL_FIRST + 3,
	IPC_ERR_MSG_NOT_AVAILABLE	= EC_ERROR_INTERNAL_FIRST + 4,
	IPC_ERR_INVALID_MSG		= EC_ERROR_INTERNAL_FIRST + 5,
};

enum ipc_peer_id {
	IPC_PEER_ID_HOST	= 0, /* x64 host */
#if 0 /* other peers are not implemented yet */
	IPC_PEER_ID_PMC		= 1, /* Power Management Controller */
	IPC_PEER_ID_CSME	= 2, /* Converged Security Management Engine */
	IPC_PEER_ID_CAVS    = 3, /* Audio, Voice, and Speech engine */
	IPC_PEER_ID_ISP 	= 4, /* Image Signal Processor */
#endif
	IPC_PEERS_COUNT,
};
/*
 * Currently ipc handle encoding only allows maximum 16 peers which is
 * enough for ISH3, ISH4, and ISH5. They have 5 peers.
 */
BUILD_ASSERT(IPC_PEERS_COUNT <= 0x0F);

enum ipc_protocol {
	IPC_PROTOCOL_BOOT = 0,	/* Not supported */
	IPC_PROTOCOL_HECI,	/* Host Embedded Controller Interface */
	IPC_PROTOCOL_MCTP,	/* not supported */
	IPC_PROTOCOL_MNG,	/* Management protocol */
	IPC_PROTOCOL_ECP,	/* EC Protocol. not supported */
	IPC_PROTOCOL_COUNT
};
/*
 * IPC handle enconding only supports 16 protocols which is the
 * maximum protocols supported by IPC doorbell encoding.
 */
BUILD_ASSERT(IPC_PROTOCOL_COUNT <= 0x0F);

typedef void *				ipc_handle_t;

#define IPC_MAX_PAYLOAD_SIZE		128
#define IPC_INVALID_HANDLE		NULL

/*
 * Open ipc channel
 *
 * @param peer_id	select peer to communicate.
 * @param protocol	select protocol
 * @param event		set event flag
 *
 * @return		ipc handle or IPC_INVALID_HANDLE if there's error
 */
ipc_handle_t ipc_open(const enum ipc_peer_id peer_id,
		      const enum ipc_protocol protocol,
		      const uint32_t event);
void ipc_close(const ipc_handle_t handle);

/*
 * Read message from ipc channel.
 * The function should be call by the same task called ipc_open().
 * The function waits until message is available.
 * @param timeout_us	if == -1, wait until message is available.
 *			if == 0, return immediately.
 *			if > 0, wait for the specified microsecond duration time
 */
int ipc_read(const ipc_handle_t handle, void *buf, const size_t buf_size,
             int timeout_us);

/* Write message to ipc channel. */
int ipc_write_timestamp(const ipc_handle_t handle, const void *buf,
	      const size_t buf_size, uint32_t *timestamp);

#endif /* __IPC_HECI_H */
