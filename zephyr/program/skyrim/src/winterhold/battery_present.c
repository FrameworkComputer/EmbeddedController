/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "battery.h"
#include "battery_smart.h"
#include "common.h"

#include <zephyr/drivers/gpio.h>

static enum battery_present batt_pres_prev = BP_NOT_SURE;

__overridable bool board_battery_is_initialized(void)
{
	int batt_status;

	return battery_status(&batt_status) != EC_SUCCESS ?
		       false :
		       !!(batt_status & STATUS_INITIALIZED);
}

/*
 * Physical detection of battery.
 */
static enum battery_present battery_check_present_status(void)
{
	enum battery_present batt_pres;

	if (battery_is_cut_off())
		return BP_NO;

	/* Get the physical hardware status */
	batt_pres = battery_hw_present();

	/*
	 * If the battery is not physically connected, then no need to perform
	 * any more checks.
	 */
	if (batt_pres == BP_NO)
		return BP_NO;

	/*
	 * If the battery is present now and was present last time we checked,
	 * return early.
	 */
	if ((batt_pres == BP_YES) && (batt_pres == batt_pres_prev))
		return BP_YES;

	/*
	 * Check battery initialization. If the battery is not initialized,
	 * then return BP_NOT_SURE. Battery could be in ship
	 * mode and might require pre-charge current to wake it up. BP_NO is not
	 * returned here because charger state machine will not provide
	 * pre-charge current assuming that battery is not present.
	 */
	if (!board_battery_is_initialized())
		return BP_NOT_SURE;

	return BP_YES;
}

enum battery_present battery_is_present(void)
{
	batt_pres_prev = battery_check_present_status();
	return batt_pres_prev;
}

enum battery_present battery_hw_present(void)
{
	const struct gpio_dt_spec *batt_pres;

	batt_pres = GPIO_DT_FROM_NODELABEL(gpio_ec_batt_pres_odl);

	/*
	 * The GPIO is low when the battery is physically present.
	 * But if battery cell voltage < 2.5V, it will not able to
	 * pull down EC_BATT_PRES_ODL. So we need to set pre-charge
	 * current even EC_BATT_PRES_ODL is high.
	 */
	return gpio_pin_get_dt(batt_pres) ? BP_NOT_SURE : BP_YES;
}
