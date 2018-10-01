/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "battery_smart.h"
#include "gpio.h"
#include "system.h"

static enum battery_present batt_pres_prev = BP_NOT_SURE;

enum battery_present battery_hw_present(void)
{
	enum battery_present bp;
	/* The GPIO is low when the battery is physically present */
	/*
	 * TODO(b/111704193): The signal GPIO_EC_BATT_PRES_ODL has an issue
	 * where it's floating (?) at ~2V when it should be low when the battery
	 * is connected. The signal will read correctly following a cold reset
	 * and the battery is connected, but following a warm reboot, it reads
	 * high. In order to allow charging to work, replacing this with the a
	 * check that the Operation Status register can be read. Once the HW
	 * issue is resolved then change this back to checking the physical
	 * presence pin.
	 *
	 */

	if (system_get_board_version() == 0)
		/* P0 boards can't use gpio signal */
		bp = (battery_get_disconnect_state() ==
		      BATTERY_DISCONNECT_ERROR) ? BP_NO : BP_YES;
	else
		/* P1 boards can read presence from gpio signal */
		bp = gpio_get_level(GPIO_EC_BATT_PRES_ODL) ? BP_NO : BP_YES;

	return bp;
}

static int battery_init(void)
{
	int batt_status;

	return battery_status(&batt_status) ? 0 :
		!!(batt_status & STATUS_INITIALIZED);
}

/*
 * Physical detection of battery.
 */
static enum battery_present battery_check_present_status(void)
{
	enum battery_present batt_pres;

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
	 * Ensure that battery is:
	 * 1. Not in cutoff
	 * 2. Initialized
	 */
	if (battery_is_cut_off() != BATTERY_CUTOFF_STATE_NORMAL ||
	    battery_init() == 0) {
		batt_pres = BP_NO;
	}

	return batt_pres;
}

enum battery_present battery_is_present(void)
{
	batt_pres_prev = battery_check_present_status();
	return batt_pres_prev;
}
