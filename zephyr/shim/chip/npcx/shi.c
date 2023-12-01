/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Functions needed by Serial Host Interface module for Chrome EC */

#include "chipset.h"
#include "drivers/cros_shi.h"
#include "hooks.h"
#include "host_command.h"
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/dt-bindings/clock/npcx_clock.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device_runtime.h>

#include <ap_power/ap_power.h>
#include <soc.h>

LOG_MODULE_REGISTER(shim_cros_shi, LOG_LEVEL_DBG);

#define SHI_NODE DT_NODELABEL(shi0)

static void shi_enable(void)
{
	const struct device *cros_shi_dev = DEVICE_DT_GET(SHI_NODE);

	if (!device_is_ready(cros_shi_dev)) {
		LOG_ERR("device %s not ready", cros_shi_dev->name);
		return;
	}

	LOG_INF("%s", __func__);
#ifndef CONFIG_EC_HOST_CMD
	cros_shi_enable(cros_shi_dev);
#else
	pm_device_runtime_get(cros_shi_dev);
#endif
}

static void shi_disable(void)
{
	const struct device *cros_shi_dev = DEVICE_DT_GET(SHI_NODE);

	if (!device_is_ready(cros_shi_dev)) {
		LOG_ERR("device %s not ready", cros_shi_dev->name);
		return;
	}

	LOG_INF("%s", __func__);
#ifndef CONFIG_EC_HOST_CMD
	cros_shi_disable(cros_shi_dev);
#else
	pm_device_runtime_put(cros_shi_dev);
#endif
}
DECLARE_HOOK(HOOK_SYSJUMP, shi_disable, HOOK_PRIO_DEFAULT);

static void shi_power_change(struct ap_power_ev_callback *cb,
			     struct ap_power_ev_data data)
{
	switch (data.event) {
	default:
		return;

#if CONFIG_PLATFORM_EC_CHIPSET_RESUME_INIT_HOOK
	case AP_POWER_RESUME_INIT:
		shi_enable();
		break;

	case AP_POWER_SUSPEND_COMPLETE:
		shi_disable();
		break;
#else
	case AP_POWER_RESUME:
		shi_enable();
		break;

	case AP_POWER_SUSPEND:
		shi_disable();
		break;
#endif
	}
}

static void shi_init(void)
{
	static struct ap_power_ev_callback cb;
#ifdef CONFIG_EC_HOST_CMD
	const struct device *cros_shi_dev = DEVICE_DT_GET(SHI_NODE);
#endif

	ap_power_ev_init_callback(&cb, shi_power_change,
#if CONFIG_PLATFORM_EC_CHIPSET_RESUME_INIT_HOOK
				  AP_POWER_RESUME_INIT |
					  AP_POWER_SUSPEND_COMPLETE
#else
				  AP_POWER_RESUME | AP_POWER_SUSPEND
#endif
	);
	ap_power_ev_add_callback(&cb);

#ifdef CONFIG_EC_HOST_CMD
	pm_device_runtime_enable(cros_shi_dev);
#endif

	if (IS_ENABLED(CONFIG_CROS_SHI_NPCX_DEBUG) ||
	    (system_jumped_late() && chipset_in_state(CHIPSET_STATE_ON))) {
		shi_enable();
	}
}
/* Call hook after chipset sets initial power state */
DECLARE_HOOK(HOOK_INIT, shi_init, HOOK_PRIO_POST_CHIPSET);

#ifndef CONFIG_EC_HOST_CMD
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
#endif /* !CONFIG_EC_HOST_CMD */
