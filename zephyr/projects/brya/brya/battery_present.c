/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "cbi.h"
#include "gpio.h"

enum battery_present battery_hw_present(void)
{
	enum gpio_signal batt_pres;

	if (get_board_id() == 1)
		batt_pres = GPIO_ID_1_EC_BATT_PRES_ODL;
	else
		batt_pres = GPIO_EC_BATT_PRES_ODL;

	/* The GPIO is low when the battery is physically present */
	return gpio_get_level(batt_pres) ? BP_NO : BP_YES;
}
