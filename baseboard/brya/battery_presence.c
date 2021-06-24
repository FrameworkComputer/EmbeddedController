/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Common battery presence checking for Brya family.
 * Each board should implement board_battery_info[] to define the specific
 * battery packs supported.
 */
#include <stdbool.h>

#include "battery.h"
#include "battery_smart.h"
#include "common.h"

static enum battery_present batt_pres_prev = BP_NOT_SURE;

__overridable bool board_battery_is_initialized(void)
{
	int batt_status;

	return battery_status(&batt_status) != EC_SUCCESS ? false :
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
