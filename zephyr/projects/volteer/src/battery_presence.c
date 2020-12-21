/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Common battery presence checking for Volteer family.
 * Each board should implement board_battery_info[] to define the specific
 * battery packs supported.
 */
#include <stdbool.h>

#include "battery.h"
#include "battery_smart.h"
#include "gpio.h"

static enum battery_present batt_pres_prev = BP_NOT_SURE;

enum battery_present battery_hw_present(void)
{
	/* The GPIO is low when the battery is physically present */
	return gpio_get_level(GPIO_EC_BATT_PRES_ODL) ? BP_NO : BP_YES;
}

static bool battery_init(void)
{
	int batt_status;

	return battery_status(&batt_status) ? 0 :
		!!(batt_status & STATUS_INITIALIZED);
}

__overridable bool board_battery_is_initialized(void)
{
	/*
	 * Set default to return true
	 */
	return true;
}

/*
 * Physical detection of battery.
 */
static enum battery_present battery_check_present_status(void)
{
	enum battery_present batt_pres;
	bool batt_initialization_state;

	/* Get the physical hardware status */
	batt_pres = battery_hw_present();

	/*
	 * If the battery is not physically connected, then no need to perform
	 * any more checks.
	 */
	if (batt_pres != BP_YES)
		return batt_pres;

	/*
	 * If the battery is present now and was present last time we checked,
	 * return early.
	 */
	if (batt_pres == batt_pres_prev)
		return batt_pres;

	/*
	 * Check battery initialization. If the battery is not initialized,
	 * then return BP_NOT_SURE. Battery could be in ship
	 * mode and might require pre-charge current to wake it up. BP_NO is not
	 * returned here because charger state machine will not provide
	 * pre-charge current assuming that battery is not present.
	 */
	batt_initialization_state = board_battery_is_initialized();
	if (!batt_initialization_state)
		return BP_NOT_SURE;
	/*
	 * Ensure that battery is:
	 * 1. Not in cutoff
	 * 2. Initialized
	 */
	if (battery_is_cut_off() || !battery_init())
		batt_pres = BP_NO;

	return batt_pres;
}

enum battery_present battery_is_present(void)
{
	batt_pres_prev = battery_check_present_status();
	return batt_pres_prev;
}
