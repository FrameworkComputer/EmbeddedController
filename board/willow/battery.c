/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

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
			},
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
			},
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
				.reg_addr = 0x0,
				.reg_mask = 0x0002,
				.disconnect_val = 0x0,
			},
			.flags = FUEL_GAUGE_FLAG_MFGACC,
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
	[BATTERY_PANASONIC_AC16L5J_KT00205009] = {
		.fuel_gauge = {
			.manuf_name = "PANASONIC KT00205009",
			.device_name = "AP16L5J",
			.ship_mode = {
				.reg_addr = 0x3A,
				.reg_data = { 0xC574, 0xC574 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x4000,
				.disconnect_val = 0x0,
			},
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
	/* LGC AP18C8K Battery Information */
	 [BATTERY_LGC_AP18C8K] = {
		.fuel_gauge = {
			.manuf_name = "LGC KT0030G020",
			.device_name = "AP18C8K",
			.ship_mode = {
				.reg_addr = 0x3A,
				.reg_data = { 0xC574, 0xC574 },
			},
			.fet = {
				.reg_addr = 0x43,
				.reg_mask = 0x0001,
				.disconnect_val = 0x0,
			},
		},
		.batt_info = {
			.voltage_max            = 13050,
			.voltage_normal         = 11250,
			.voltage_min            = 9000,
			.precharge_current      = 256,
			.start_charging_min_c   = 0,
			.start_charging_max_c   = 50,
			.charging_min_c         = 0,
			.charging_max_c         = 60,
			.discharging_min_c      = -20,
			.discharging_max_c      = 75,
		},
	},
	/* Murata AP18C4K Battery Information */
	[BATTERY_MURATA_AP18C4K] = {
		.fuel_gauge = {
			.manuf_name = "Murata KT00304012",
			.device_name = "AP18C4K",
			.ship_mode = {
				.reg_addr = 0x3A,
				.reg_data = { 0xC574, 0xC574 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x2000,
				.disconnect_val = 0x2000,
			},
		},
		.batt_info = {
			.voltage_max		= 13200,
			.voltage_normal		= 11400,
			.voltage_min		= 9000,
			.precharge_current	= 256,
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 75,
		},
	},
	[BATTERY_PANASONIC_AP19B5K_KT00305011] = {
		.fuel_gauge = {
			.manuf_name = "PANASONIC KT00305011",
			.device_name = "AP19B5K",
			.ship_mode = {
				.reg_addr = 0x3A,
				.reg_data = { 0xC574, 0xC574 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x4000,
				.disconnect_val = 0x0,
			},
		},
		.batt_info = {
			.voltage_max		= 13200,
			.voltage_normal		= 11550,
			.voltage_min		= 9000,
			.precharge_current	= 256,
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 75,
		},
	},
	/* LGC AP19B8K Battery Information */
	 [BATTERY_LGC_AP19B8K] = {
		.fuel_gauge = {
			.manuf_name = "LGC KT0030G022",
			.device_name = "AP19B8K",
			.ship_mode = {
				.reg_addr = 0x3A,
				.reg_data = { 0xC574, 0xC574 },
			},
			.fet = {
				.reg_addr = 0x43,
				.reg_mask = 0x0001,
				.disconnect_val = 0x0,
			},
		},
		.batt_info = {
			.voltage_max            = 13050,
			.voltage_normal         = 11250,
			.voltage_min            = 9000,
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
