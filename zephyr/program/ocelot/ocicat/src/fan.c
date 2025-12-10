/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cros_cbi.h"
#include "fan.h"
#include "gpio/gpio.h"
#include "hooks.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(fan_init, LOG_LEVEL_INF);

test_export_static void fan_init(void)
{
	int ret;
	uint32_t val;
	/*
	 * Retrieve the fan config.
	 */
	ret = cros_cbi_get_fw_config(FW_THERMAL_SOLUTION, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d",
			FW_THERMAL_SOLUTION);
		return;
	}
	if (val == FW_THERMAL_SOLUTION_15W) {
		LOG_INF("FW_THERMAL_SOLUTION_15W, offset: %d",
			FW_THERMAL_SOLUTION);
	} else {
		/* Disable the fan */
		fan_set_count(0);
		LOG_INF("FW_THERMAL_SOLUTION_6W, offset: %d",
			FW_THERMAL_SOLUTION);
	}
}
DECLARE_HOOK(HOOK_INIT, fan_init, HOOK_PRIO_POST_FIRST);
