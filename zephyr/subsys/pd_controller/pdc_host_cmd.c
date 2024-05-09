/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "usb_pd.h"
#include "usbc/pdc_power_mgmt.h"

#include <string.h>

#include <zephyr/device.h>

#include <drivers/pdc.h>

#ifdef CONFIG_PLATFORM_EC_HOSTCMD_PD_CHIP_INFO
/* EC_CMD_PD_CHIP_INFO implementation when a PDC is used. */

static enum ec_status hc_remote_pd_chip_info(struct host_cmd_handler_args *args)
{
	const struct ec_params_pd_chip_info *p = args->params;
	struct ec_response_pd_chip_info_v1 resp = { 0 };
	struct pdc_info_t pdc_info;

	if (pdc_power_mgmt_get_info(p->port, &pdc_info, p->live)) {
		return EC_RES_ERROR;
	}

	resp.vendor_id = PDC_VIDPID_GET_VID(pdc_info.vid_pid);
	resp.product_id = PDC_VIDPID_GET_PID(pdc_info.vid_pid);

	/* Ver output is 3 bytes right-aligned in a 32-bit container. Map into
	 * the first three bytes of fw_version_string.
	 */

	resp.fw_version_string[2] = PDC_FWVER_GET_MAJOR(pdc_info.fw_version);
	resp.fw_version_string[1] = PDC_FWVER_GET_MINOR(pdc_info.fw_version);
	resp.fw_version_string[0] = PDC_FWVER_GET_PATCH(pdc_info.fw_version);

	/*
	 * Take advantage of the fact that v0 and v1 structs have the
	 * same layout for v0 data. (v1 just appends data)
	 */
	args->response_size =
		args->version ? sizeof(struct ec_response_pd_chip_info_v1) :
				sizeof(struct ec_response_pd_chip_info);

	memcpy(args->response, &resp, args->response_size);

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_PD_CHIP_INFO, hc_remote_pd_chip_info,
		     EC_VER_MASK(0) | EC_VER_MASK(1));
#endif /* CONFIG_HOSTCMD_PD_CHIP_INFO */

static enum ec_status hc_pd_ports(struct host_cmd_handler_args *args)
{
	struct ec_response_usb_pd_ports *r = args->response;

	r->num_ports = pdc_power_mgmt_get_usb_pd_port_count();
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_PORTS, hc_pd_ports, EC_VER_MASK(0));

#if !defined(CONFIG_USB_PD_ALTMODE_INTEL)
uint8_t get_pd_control_flags(int port)
{
	/* To support EC_CMD_USB_PD_CONTROL  */
	return 0;
}
#endif
