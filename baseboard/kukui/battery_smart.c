/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "battery_smart.h"
#include "timer.h"
#include "util.h"

enum battery_present batt_pres_prev = BP_NOT_SURE;

/*
 * Physical detection of battery.
 */
static enum battery_present battery_check_present_status(void)
{
	enum battery_present batt_pres = BP_NOT_SURE;

#ifdef CONFIG_BATTERY_HW_PRESENT_CUSTOM
	/* Get the physical hardware status */
	batt_pres = battery_hw_present();
#endif

	/*
	 * If the battery is not physically connected, then no need to perform
	 * any more checks.
	 */
	if (batt_pres == BP_NO)
		return batt_pres;

	/*
	 * If the battery is present now and was present last time we checked,
	 * return early.
	 */
	if (batt_pres == batt_pres_prev)
		return batt_pres;

	/*
	 * Check battery disconnect status. If we are unable to read battery
	 * disconnect status, then return BP_NOT_SURE. Battery could be in ship
	 * mode and might require pre-charge current to wake it up. BP_NO is not
	 * returned here because charger state machine will not provide
	 * pre-charge current assuming that battery is not present.
	 */
	if (battery_get_disconnect_state() == BATTERY_DISCONNECT_ERROR)
		return BP_NOT_SURE;

	/* Ensure the battery is not in cutoff state */
	if (battery_is_cut_off() != BATTERY_CUTOFF_STATE_NORMAL)
		return BP_NO;

	return batt_pres;
}

enum battery_present battery_is_present(void)
{
	batt_pres_prev = battery_check_present_status();
	return batt_pres_prev;
}

#ifdef CONFIG_I2C_BITBANG_CROS_EC
static void fix_single_param(int flag, int *cached, int *curr)
{
	if (flag)
		*curr = *cached;
	else
		*cached = *curr;
}

#define CACHE_INVALIDATION_TIME_US (5 * SECOND)

/*
 * b:144195782: bitbang fails randomly, and there's no way to
 * notify kernel side that bitbang read failed.
 * Thus, if any value in batt_params is bad, replace it with a cached
 * good value, to make sure we never send random numbers to kernel
 * side.
 */
__override void board_battery_compensate_params(struct batt_params *batt)
{
	static struct batt_params batt_cache = { 0 };
	static timestamp_t deadline;

	/*
	 * If battery keeps failing for 5 seconds, stop hiding the error and
	 * report back to host.
	 */
	if (batt->flags & BATT_FLAG_BAD_ANY) {
		if (timestamp_expired(deadline, NULL))
			return;
	} else {
		deadline.val = get_time().val + CACHE_INVALIDATION_TIME_US;
	}

	/* return cached values for at most CACHE_INVALIDATION_TIME_US */
	fix_single_param(batt->flags & BATT_FLAG_BAD_STATE_OF_CHARGE,
			 &batt_cache.state_of_charge, &batt->state_of_charge);
	fix_single_param(batt->flags & BATT_FLAG_BAD_VOLTAGE,
			 &batt_cache.voltage, &batt->voltage);
	fix_single_param(batt->flags & BATT_FLAG_BAD_CURRENT,
			 &batt_cache.current, &batt->current);
	fix_single_param(batt->flags & BATT_FLAG_BAD_DESIRED_VOLTAGE,
			 &batt_cache.desired_voltage, &batt->desired_voltage);
	fix_single_param(batt->flags & BATT_FLAG_BAD_DESIRED_CURRENT,
			 &batt_cache.desired_current, &batt->desired_current);
	fix_single_param(batt->flags & BATT_FLAG_BAD_REMAINING_CAPACITY,
			 &batt_cache.remaining_capacity,
			 &batt->remaining_capacity);
	fix_single_param(batt->flags & BATT_FLAG_BAD_FULL_CAPACITY,
			 &batt_cache.full_capacity, &batt->full_capacity);
	fix_single_param(batt->flags & BATT_FLAG_BAD_STATUS, &batt_cache.status,
			 &batt->status);
	fix_single_param(batt->flags & BATT_FLAG_BAD_TEMPERATURE,
			 &batt_cache.temperature, &batt->temperature);
	/*
	 * If battery_compensate_params() didn't calculate display_charge
	 * for us, also update it with last good value.
	 */
	fix_single_param(batt->display_charge == 0, &batt_cache.display_charge,
			 &batt->display_charge);

	/* remove bad flags after applying cached values */
	batt->flags &= ~BATT_FLAG_BAD_ANY;
}
#endif /* CONFIG_I2C_BITBANG_CROS_EC */
