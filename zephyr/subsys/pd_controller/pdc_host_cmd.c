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
	struct ec_response_pd_chip_info_v3 resp = { 0 };
	struct pdc_info_t pdc_info;

	/* Safety check to make sure the pdc_info_t struct and host command use
	 * the same project name length.
	 */
	BUILD_ASSERT(sizeof(resp.fw_name_str) == sizeof(pdc_info.project_name));

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

	/* Look up correct response size based on version. All support the
	 * basic fields set above.
	 */
	if (args->version == 0) {
		args->response_size = sizeof(struct ec_response_pd_chip_info);

		/* All V0 fields populated above */
	} else if (args->version == 1) {
		args->response_size =
			sizeof(struct ec_response_pd_chip_info_v1);

		/* PDC doesn't use the min_req_fw_version_string field added in
		 * V1.
		 */
	} else if (args->version >= 2) {
		args->response_size =
			sizeof(struct ec_response_pd_chip_info_v2);

		/* Fill in V2-specific info. `fw_name_str` must be NUL-
		 * terminated
		 */
		resp.fw_update_flags = 0;
		strncpy(resp.fw_name_str, pdc_info.project_name,
			sizeof(resp.fw_name_str));
	}
	if (args->version >= 3) {
		args->response_size =
			sizeof(struct ec_response_pd_chip_info_v3);

		/* Fill in V3-specific info. `driver_name` must be NUL-
		 * terminated
		 */
		strncpy(resp.driver_name, pdc_info.driver_name,
			sizeof(resp.driver_name));
	}

	memcpy(args->response, &resp, args->response_size);

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_PD_CHIP_INFO, hc_remote_pd_chip_info,
		     EC_VER_MASK(0) | EC_VER_MASK(1) | EC_VER_MASK(2) |
			     EC_VER_MASK(3));
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
