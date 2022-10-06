/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "fan.h"
#include "gpio/gpio.h"
#include "hooks.h"

LOG_MODULE_DECLARE(skyrim, CONFIG_SKYRIM_LOG_LEVEL);

/*
 * Skyrim fan support
 */
static void fan_init(void)
{
	int ret;
	uint32_t val;
	uint32_t board_version;
	/*
	 * Retrieve the fan config.
	 */
	ret = cros_cbi_get_fw_config(FW_FAN, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_FAN);
		return;
	}

	ret = cbi_get_board_version(&board_version);
	if (ret != EC_SUCCESS) {
		LOG_ERR("Error retrieving CBI board version");
		return;
	}

	if ((board_version >= 3) && (val != FW_FAN_PRESENT)) {
		/* Disable the fan */
		fan_set_count(0);
	}
}
DECLARE_HOOK(HOOK_INIT, fan_init, HOOK_PRIO_POST_FIRST);
