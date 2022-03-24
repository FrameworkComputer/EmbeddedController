/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Functions needed by Serial Host Interface module for Chrome EC */

#include <device.h>
#include <dt-bindings/clock/npcx_clock.h>
#include <logging/log.h>
#include <soc.h>
#include <zephyr.h>

#include <ap_power/ap_power.h>
#include "chipset.h"
#include "drivers/cros_shi.h"
#include "hooks.h"
#include "host_command.h"
#include "system.h"

LOG_MODULE_REGISTER(shim_cros_shi, LOG_LEVEL_DBG);

#define SHI_NODE DT_NODELABEL(shi)

static void shi_enable(void)
{
	const struct device *cros_shi_dev = DEVICE_DT_GET(SHI_NODE);

	if (!device_is_ready(cros_shi_dev)) {
		LOG_ERR("Error: device %s is not ready", cros_shi_dev->name);
		return;
	}

	LOG_INF("%s", __func__);
	cros_shi_enable(cros_shi_dev);
}

static void shi_disable(void)
{
	const struct device *cros_shi_dev = DEVICE_DT_GET(SHI_NODE);

	if (!device_is_ready(cros_shi_dev)) {
		LOG_ERR("Error: device %s is not ready", cros_shi_dev->name);
		return;
	}

	LOG_INF("%s", __func__);
	cros_shi_disable(cros_shi_dev);
}
DECLARE_HOOK(HOOK_SYSJUMP, shi_disable, HOOK_PRIO_DEFAULT);

static void shi_power_change(struct ap_power_ev_callback *cb,
			     struct ap_power_ev_data data)
{
	switch (data.event) {
	default:
		return;

#ifdef CONFIG_CHIPSET_RESUME_INIT_HOOK
	case AP_POWER_RESUME_INIT:
		shi_enable();
		break;

	case AP_POWER_SHUTDOWN_COMPLETE:
		shi_disable();
		break;
#else
	case AP_POWER_RESUME:
		shi_enable();
		break;

	case AP_POWER_SHUTDOWN:
		shi_disable();
		break;
#endif
	}
}

static int shi_init(const struct device *unused)
{
	ARG_UNUSED(unused);
	static struct ap_power_ev_callback cb;

	ap_power_ev_init_callback(&cb, shi_power_change,
#ifdef CONFIG_CHIPSET_RESUME_INIT_HOOK
				  AP_POWER_RESUME_INIT |
				  AP_POWER_SHUTDOWN_COMPLETE
#else
				  AP_POWER_RESUME |
				  AP_POWER_SUSPEND
#endif
				  );
	ap_power_ev_add_callback(&cb);

	if (IS_ENABLED(CONFIG_CROS_SHI_NPCX_DEBUG) ||
	    (system_jumped_late() && chipset_in_state(CHIPSET_STATE_ON))) {
		shi_enable();
	}

	return 0;
}
/* Call hook after chipset sets initial power state */
SYS_INIT(shi_init, APPLICATION, HOOK_PRIO_POST_CHIPSET);

/* Get protocol information */
static enum ec_status shi_get_protocol_info(struct host_cmd_handler_args *args)
{
	struct ec_response_get_protocol_info *r = args->response;

	memset(r, '\0', sizeof(*r));
	r->protocol_versions = BIT(3);
	r->max_request_packet_size = CONFIG_CROS_SHI_MAX_REQUEST;
	r->max_response_packet_size = CONFIG_CROS_SHI_MAX_RESPONSE;
	r->flags = EC_PROTOCOL_INFO_IN_PROGRESS_SUPPORTED;

	args->response_size = sizeof(*r);

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PROTOCOL_INFO, shi_get_protocol_info,
		     EC_VER_MASK(0));
