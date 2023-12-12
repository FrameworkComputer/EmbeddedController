/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "battery.h"

#include <zephyr/drivers/gpio.h>

/*
 * Physical detection of battery.
 */
enum battery_present battery_hw_present(void)
{
	const struct gpio_dt_spec *batt_pres;

	batt_pres = GPIO_DT_FROM_NODELABEL(gpio_ec_batt_pres_odl);

	/* The GPIO is low when the battery is physically present */
	return gpio_pin_get_dt(batt_pres) ? BP_NO : BP_YES;
}

enum battery_present battery_is_present(void)
{
	enum battery_present batt_pres;

	if (battery_is_cut_off()) {
		return BP_NO;
	}

	/* Get the physical hardware status */
	batt_pres = battery_hw_present();

	return batt_pres;
}
