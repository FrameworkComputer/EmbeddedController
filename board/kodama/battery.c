/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "charge_state.h"
#include "charger_mt6370.h"
#include "console.h"
#include "driver/charger/rt946x.h"
#include "gpio.h"
#include "power.h"
#include "timer.h"
#include "usb_pd.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

const struct board_batt_params board_battery_info[] = {
	[BATTERY_SIMPLO] = {
		.fuel_gauge = {
			.manuf_name = "SMP",
			.device_name = "L19M3PG0",
			.ship_mode = {
				.reg_addr = 0x34,
				.reg_data = { 0x0000, 0x1000 },
			},
			.fet = {
				.reg_addr = 0x34,
				.reg_mask = 0x0100,
				.disconnect_val = 0x0100,
			}
		},
		.batt_info = {
			.voltage_max		= 4400,
			.voltage_normal		= 3840,
			.voltage_min		= 3000,
			.precharge_current	= 256,
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 45,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 60,
		},
	},
	[BATTERY_CELXPERT] = {
		.fuel_gauge = {
			.manuf_name = "Celxpert",
			.device_name = "L19C3PG0",
			.ship_mode = {
				.reg_addr = 0x34,
				.reg_data = { 0x0000, 0x1000 },
			},
			.fet = {
				.reg_addr = 0x34,
				.reg_mask = 0x0100,
				.disconnect_val = 0x0100,
			}
		},
		.batt_info = {
			.voltage_max		= 4400,
			.voltage_normal		= 3840,
			.voltage_min		= 2800,
			.precharge_current	= 404,
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 45,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 60,
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_SIMPLO;

enum battery_present battery_hw_present(void)
{
	return gpio_get_level(GPIO_EC_BATT_PRES_ODL) ? BP_NO : BP_YES;
}

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
			&batt_cache.state_of_charge,
			&batt->state_of_charge);
	fix_single_param(batt->flags & BATT_FLAG_BAD_VOLTAGE,
			&batt_cache.voltage,
			&batt->voltage);
	fix_single_param(batt->flags & BATT_FLAG_BAD_CURRENT,
			&batt_cache.current,
			&batt->current);
	fix_single_param(batt->flags & BATT_FLAG_BAD_DESIRED_VOLTAGE,
			&batt_cache.desired_voltage,
			&batt->desired_voltage);
	fix_single_param(batt->flags & BATT_FLAG_BAD_DESIRED_CURRENT,
			&batt_cache.desired_current,
			&batt->desired_current);
	fix_single_param(batt->flags & BATT_FLAG_BAD_REMAINING_CAPACITY,
			&batt_cache.remaining_capacity,
			&batt->remaining_capacity);
	fix_single_param(batt->flags & BATT_FLAG_BAD_FULL_CAPACITY,
			&batt_cache.full_capacity,
			&batt->full_capacity);
	fix_single_param(batt->flags & BATT_FLAG_BAD_STATUS,
			&batt_cache.status,
			&batt->status);
	fix_single_param(batt->flags & BATT_FLAG_BAD_TEMPERATURE,
			&batt_cache.temperature,
			&batt->temperature);
	/*
	 * If battery_compensate_params() didn't calculate display_charge
	 * for us, also update it with last good value.
	 */
	fix_single_param(batt->display_charge == 0,
			&batt_cache.display_charge,
			&batt->display_charge);

	/* remove bad flags after applying cached values */
	batt->flags &= ~BATT_FLAG_BAD_ANY;
}

int charger_profile_override(struct charge_state_data *curr)
{
	const struct battery_info *batt_info = battery_get_info();
	/* battery temp in 0.1 deg C */
	int bat_temp_c = curr->batt.temperature - 2731;

#ifdef VARIANT_KUKUI_CHARGER_MT6370
	mt6370_charger_profile_override(curr);
#endif /* CONFIG_CHARGER_MT6370 */

	/*
	 * When smart battery temperature is more than 45 deg C, the max
	 * charging voltage is 4100mV.
	 */
	if (curr->state == ST_CHARGE && bat_temp_c >= 450
		&& !(curr->batt.flags & BATT_FLAG_BAD_TEMPERATURE))
		curr->requested_voltage	= 4100;
	else
		curr->requested_voltage = batt_info->voltage_max;

	/*
	 * mt6370's minimum regulated current is 500mA REG17[7:2] 0b100,
	 * values below 0b100 are preserved. In the other hand, it makes sure
	 * mt6370's VOREG set as 4400mV and minimum value of mt6370's ICHG
	 * is limited as 500mA.
	 */
	curr->requested_current = MAX(500, curr->requested_current);

	return 0;
}

enum ec_status charger_profile_override_get_param(uint32_t param,
						  uint32_t *value)
{
	return EC_RES_INVALID_PARAM;
}

enum ec_status charger_profile_override_set_param(uint32_t param,
						  uint32_t value)
{
	return EC_RES_INVALID_PARAM;
}
