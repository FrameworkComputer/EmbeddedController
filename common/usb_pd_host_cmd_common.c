/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Host commands shared across multiple USB-PD implementations
 */

#include "atomic.h"
#include "ec_commands.h"
#include "host_command.h"
#include "usb_pd.h"

__overridable enum ec_pd_port_location board_get_pd_port_location(int port)
{
	(void)port;
	return EC_PD_PORT_LOCATION_UNKNOWN;
}

static enum ec_status hc_get_pd_port_caps(struct host_cmd_handler_args *args)
{
	const struct ec_params_get_pd_port_caps *p = args->params;
	struct ec_response_get_pd_port_caps *r = args->response;

	if (p->port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	/* Power Role */
	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE))
		r->pd_power_role_cap = EC_PD_POWER_ROLE_DUAL;
	else
		r->pd_power_role_cap = EC_PD_POWER_ROLE_SINK;

	/* Try-Power Role */
	if (IS_ENABLED(CONFIG_USB_PD_TRY_SRC))
		r->pd_try_power_role_cap = EC_PD_TRY_POWER_ROLE_SOURCE;
	else
		r->pd_try_power_role_cap = EC_PD_TRY_POWER_ROLE_NONE;

	if (IS_ENABLED(CONFIG_USB_VPD) || IS_ENABLED(CONFIG_USB_CTVPD))
		r->pd_data_role_cap = EC_PD_DATA_ROLE_UFP;
	else
		r->pd_data_role_cap = EC_PD_DATA_ROLE_DUAL;

	/* Allow boards to override the locations from UNKNOWN if desired */
	r->pd_port_location = board_get_pd_port_location(p->port);

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PD_PORT_CAPS, hc_get_pd_port_caps,
		     EC_VER_MASK(0));

#if !defined(CONFIG_USB_PD_TCPM_STUB)
/*
 * PD host event status for host command
 * Note: this variable must be aligned on 4-byte boundary because we pass the
 * address to atomic_ functions which use assembly to access them.
 */
static atomic_t pd_host_event_status __aligned(4);

test_mockable void pd_send_host_event(int mask)
{
	/* mask must be set */
	if (!mask)
		return;

	atomic_or(&pd_host_event_status, mask);
	/* interrupt the AP */
	host_set_single_event(EC_HOST_EVENT_PD_MCU);
}

static enum ec_status
hc_pd_host_event_status(struct host_cmd_handler_args *args)
{
	struct ec_response_host_event_status *r = args->response;

	/* Read and clear the host event status to return to AP */
	r->status = atomic_clear(&pd_host_event_status);

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PD_HOST_EVENT_STATUS, hc_pd_host_event_status,
		     EC_VER_MASK(0));
#endif /* ! CONFIG_USB_PD_TCPM_STUB */
