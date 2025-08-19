/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cros_cbi.h"
#include "hooks.h"
#include "tablet_mode.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(uldrenite_sensor, LOG_LEVEL_INF);

static void gmr_init(void)
{
	int ec_fwconfig;
	int ret = cros_cbi_get_fw_config(FORM_FACTOR, &ec_fwconfig);

	if (ret < 0) {
		LOG_ERR("error retriving CBI config: %d", ret);
		return;
	}

	if (ec_fwconfig == FORM_FACTOR_CLAMSHELL) {
		gmr_tablet_switch_disable();
		LOG_INF("Board is Clamshell");
	} else if (ec_fwconfig == FORM_FACTOR_CONVERTIBLE) {
		LOG_INF("Board is Convertible");
	}
}
DECLARE_HOOK(HOOK_INIT, gmr_init, HOOK_PRIO_DEFAULT);
