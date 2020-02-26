/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "baseboard/kukui/battery_smart.h"
#include "battery.h"
#include "battery_fuel_gauge.h"
#include "gpio.h"

const struct board_batt_params board_battery_info[] = {
	[BATTERY_PANASONIC_AC15A3J] = {
		.fuel_gauge = {
			.manuf_name = "PANASONIC",
			.device_name = "AC15A3J",
			.ship_mode = {
				.reg_addr = 0x3A,
				.reg_data = { 0xC574, 0xC574 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x4000,
				.disconnect_val = 0x0,
			}
		},
		.batt_info = {
			.voltage_max		= 13200,
			.voltage_normal		= 11580,
			.voltage_min		= 9000,
			.precharge_current	= 256,
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 45,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 60,
		},
	},
	[BATTERY_PANASONIC_AC16L5J] = {
		.fuel_gauge = {
			.manuf_name = "PANASONIC",
			.device_name = "AP16L5J",
			.ship_mode = {
				.reg_addr = 0x3A,
				.reg_data = { 0xC574, 0xC574 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x4000,
				.disconnect_val = 0x0,
			}
		},
		.batt_info = {
			.voltage_max		= 8800,
			.voltage_normal		= 7700,
			.voltage_min		= 6000,
			.precharge_current	= 256,
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 75,
		},
	},
        [BATTERY_LGC_AC16L8J] = {
                .fuel_gauge = {
                        .manuf_name = "LGC KT0020G010",
                        .device_name = "AP16L8J",
                        .ship_mode = {
                                .reg_addr = 0x3A,
                                .reg_data = { 0xC574, 0xC574 },
                        },
                        .fet = {
				.mfgacc_support = 1,
				.reg_addr = 0x0,
				.reg_mask = 0x0002,
				.disconnect_val = 0x0,
                        }
                },
                .batt_info = {
                        .voltage_max            = 8700,
                        .voltage_normal         = 7500,
                        .voltage_min            = 6000,
                        .precharge_current      = 256,
                        .start_charging_min_c   = 0,
                        .start_charging_max_c   = 50,
                        .charging_min_c         = 0,
                        .charging_max_c         = 60,
                        .discharging_min_c      = -20,
                        .discharging_max_c      = 75,
                },
        },
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_PANASONIC_AC15A3J;

enum battery_present battery_hw_present(void)
{
	return gpio_get_level(GPIO_EC_BATT_PRES_ODL) ? BP_NO : BP_YES;
}

/*
 * Physical detection of battery.
 */
__override enum battery_present battery_check_present_status(void)
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
	 * disconnect status or DFET is off, then return BP_NOT_SURE. Battery
	 * could be in ship mode and might require pre-charge current to wake
	 * it up. BP_NO is not returned here because charger state machine
	 * will not provide pre-charge current assuming that battery is not
	 * present.
	 */
	if (battery_get_disconnect_state() != BATTERY_NOT_DISCONNECTED)
		return BP_NOT_SURE;

	/*
	 * Ensure that battery is:
	 * 1. Not in cutoff
	 */
	if (battery_is_cut_off() != BATTERY_CUTOFF_STATE_NORMAL)
		return BP_NO;

	return batt_pres;
}

