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
#ifdef CONFIG_CHIPSET_RESUME_INIT_HOOK
DECLARE_HOOK(HOOK_CHIPSET_RESUME_INIT, shi_enable, HOOK_PRIO_DEFAULT);
#else
DECLARE_HOOK(HOOK_CHIPSET_RESUME, shi_enable, HOOK_PRIO_DEFAULT);
#endif

static void shi_reenable_on_sysjump(void)
{
	if (IS_ENABLED(CONFIG_CROS_SHI_NPCX_DEBUG) ||
	    (system_jumped_late() && chipset_in_state(CHIPSET_STATE_ON))) {
		shi_enable();
	}
}
/* Call hook after chipset sets initial power state */
DECLARE_HOOK(HOOK_INIT, shi_reenable_on_sysjump, HOOK_PRIO_INIT_CHIPSET + 1);

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
#ifdef CONFIG_CHIPSET_RESUME_INIT_HOOK
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND_COMPLETE, shi_disable, HOOK_PRIO_DEFAULT);
#else
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, shi_disable, HOOK_PRIO_DEFAULT);
#endif
DECLARE_HOOK(HOOK_SYSJUMP, shi_disable, HOOK_PRIO_DEFAULT);

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
