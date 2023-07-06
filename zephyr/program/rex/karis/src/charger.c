/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "charge_manager.h"
#include "charger.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(rex, CONFIG_REX_LOG_LEVEL);

__override int board_get_leave_safe_mode_delay_ms(void)
{
	const struct fuel_gauge_info *const fuel_gauge =
		&get_batt_params()->fuel_gauge;

	/* If it's COSMX battery, there's need more delay time. */
	if (!strcasecmp(fuel_gauge->manuf_name, "COSMX KT0030B004"))
		return 2000;
	else
		return 500;
}
