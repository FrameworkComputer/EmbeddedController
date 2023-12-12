/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "../../host_service_common.h"
#include "bsp_helper.h"
#include "heci_intf.h"

#include <string.h>

#include <zephyr/drivers/ipm.h>

#include <host_bsp_service.h>
#include <sedi_driver_ipc.h>

#define IPM_NAME DEVICE_DT_NAME(DT_NODELABEL(ipmhost))

static const struct device *dev;
static int ipm_intf_init(void);

uint32_t in_drbl;
uint8_t *in_data;

static int read_host_msg(uint32_t *drbl, uint8_t *msg, uint32_t msg_size)
{
	*drbl = in_drbl;
	if (msg && msg_size) {
		if (in_data == NULL) {
			return -1;
		}
		memcpy(msg, in_data, msg_size);
	}
	return 0;
}

static int send_host_msg(uint32_t drbl, uint8_t *msg, uint32_t msg_size)
{
	return ipm_send(dev, 1, drbl, msg, msg_size);
}

static int send_host_ack(void)
{
	ipm_complete(dev);
	return 0;
}

#define FWST_REG_ADDR 0x4100034
#define FWST_READY 0x3
void ipm_ready_set(uint32_t is_ready)
{
	uint32_t fwst;

	if (is_ready == 0) {
		/* Not support */
		return;
	}

	fwst = sys_read32(FWST_REG_ADDR);
	sys_write32(fwst | FWST_READY, FWST_REG_ADDR);
}

HECI_INTF_DEFINE(host)
struct heci_bsp_t ipm_bsp = { .core_id = CONFIG_HECI_CORE_ID,
			      .peer_is_host = 1,
			      .max_fragment_size = IPC_DATA_LEN_MAX,
			      .poll_write_support = 0,
			      .mng_msg_support = 1,
			      .read_msg = read_host_msg,
			      .send_msg = send_host_msg,
			      .send_ack = send_host_ack,
			      .poll_send_msg = NULL,
			      .init = ipm_intf_init,
			      .set_ready = ipm_ready_set };

static void ipm_rx_handler(const struct device *dev, void *user_data,
			   uint32_t id, volatile void *data)
{
	ARG_UNUSED(data);
	in_drbl = id;
	in_data = (void *)data;
	send_heci_newmsg_notify(&ipm_bsp);
}

static int ipm_intf_init(void)
{
	dev = device_get_binding(IPM_NAME);
	ipm_register_callback(dev, ipm_rx_handler, NULL);
	ipm_set_enabled(dev, 1);
	return 0;
}
