/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/drivers/gpio.h>

#include "battery.h"
#include "battery_smart.h"
#include "battery_fuel_gauge.h"

static enum battery_present batt_pres_prev = BP_NOT_SURE;

const struct battery_info *battery_get_info(void)
{
	return &board_battery_info->batt_info;
}

enum battery_present battery_is_present(void)
{
	enum battery_present batt_pres = BP_NOT_SURE;

    /*
     * TODO: implement battery present function.
     */

    batt_pres_prev = batt_pres;

	return batt_pres;
}
