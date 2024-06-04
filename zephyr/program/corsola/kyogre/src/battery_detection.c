/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

void enable_battery_detection(void)
{
	/* enable battery detection */
	gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(ec_batt_pres_odl),
			      GPIO_INPUT);
}
DECLARE_DEFERRED(enable_battery_detection);

void board_battery_detection_init(void)
{
	/* disable battery detection for 1000ms */
	gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(ec_batt_pres_odl),
			      GPIO_OUTPUT_HIGH);
	hook_call_deferred(&enable_battery_detection_data, 1000 * MSEC);
}
DECLARE_HOOK(HOOK_INIT, board_battery_detection_init, HOOK_PRIO_DEFAULT);
