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
	/*
	 * Retrieve the fan config.
	 */
	if (cros_cbi_ufsc_check_match(CBI_UFSC_VALUE_ID(
		    DT_NODELABEL(ufsc_fw_thermal_solution_15w)))) {
		/* Fan is present */
		LOG_INF("FW_THERMAL_SOLUTION_15W");
	} else if (cros_cbi_ufsc_check_match(CBI_UFSC_VALUE_ID(
			   DT_NODELABEL(ufsc_fw_thermal_solution_6w)))) {
		/* Disable the fan */
		fan_set_count(0);
		LOG_INF("FW_THERMAL_SOLUTION_6W");
	} else {
		LOG_INF("Error retrieving CBI USFC field");
		return;
	}
}
DECLARE_HOOK(HOOK_INIT, fan_init, HOOK_PRIO_POST_FIRST);
