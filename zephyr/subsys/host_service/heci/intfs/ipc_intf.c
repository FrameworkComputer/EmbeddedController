/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "../../host_service_common.h"
#include "bsp_helper.h"
#include "heci_intf.h"

#include <host_bsp_service.h>
#include <sedi_driver_ipc.h>

#define IPC_NAME DT_LABEL(DT_NODELABEL(ipchost))

static const struct device *dev;
static int ipc_intf_init(void);

static int read_host_msg(uint32_t *drbl, uint8_t *msg, uint32_t msg_size)
{
	return ipc_read_msg(dev, drbl, msg, msg_size);
}

static int send_host_msg(uint32_t drbl, uint8_t *msg, uint32_t msg_size)
{
	return ipc_write_msg(dev, drbl, msg, msg_size, NULL, NULL, 0);
}

static int send_host_ack(void)
{
	return ipc_send_ack(dev, 0, NULL, 0);
}

HECI_INTF_DEFINE(host)
heci_bsp_t ipc_bsp = { .core_id = CONFIG_HECI_CORE_ID,
		       .peer_is_host = 1,
		       .max_fragment_size = IPC_DATA_LEN_MAX,
		       .poll_write_support = 0,
		       .mng_msg_support = 1,
		       .read_msg = read_host_msg,
		       .send_msg = send_host_msg,
		       .send_ack = send_host_ack,
		       .poll_send_msg = NULL,
		       .init = ipc_intf_init,
		       .fwst_set = sedi_fwst_set };

static int ipc_rx_handler(const struct device *dev, void *arg)
{
	ARG_UNUSED(arg);
#if CONFIG_RTD3
#define MNG_D0_NOTIFY 9
	uint32_t inbound_drbl;
	int cmd;
	uint8_t protocol;

	struct k_sem sem_rtd3;

	ipc_read_drbl(dev, &inbound_drbl);
	cmd = HEADER_GET_MNG_CMD(inbound_drbl);
	protocol = HEADER_GET_PROTOCOL(inbound_drbl);

	if ((protocol == IPC_PROTOCOL_MNG) && (cmd == MNG_D0_NOTIFY)) {
		k_sem_give(&sem_rtd3);
	}
#endif
	send_heci_newmsg_notify(&ipc_bsp);
	return 0;
}

static int ipc_intf_init(void)
{
	dev = device_get_binding(IPC_NAME);
	ipc_set_rx_notify(dev, ipc_rx_handler);
	return 0;
}
