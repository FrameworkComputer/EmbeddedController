/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <drivers/gpio.h>

#include "battery.h"
#include "cbi.h"

enum battery_present battery_hw_present(void)
{
	const struct gpio_dt_spec *batt_pres;

	if (get_board_id() == 1)
		batt_pres = GPIO_DT_FROM_NODELABEL(gpio_id_1_ec_batt_pres_odl);
	else
		batt_pres = GPIO_DT_FROM_NODELABEL(gpio_ec_batt_pres_odl);

	/* The GPIO is low when the battery is physically present */
	return gpio_pin_get_dt(batt_pres) ? BP_NO : BP_YES;
}
