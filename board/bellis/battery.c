/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "gpio.h"

const struct board_batt_params board_battery_info[] = {
	/* LGC L20L3PG2, Gauge IC: RAJ240047A20DNP. */
	[BATTERY_LGC] = {
		.fuel_gauge = {
			.manuf_name = "LGC",
			.device_name = "L20L3PG2",
			.ship_mode = {
				.reg_addr = 0x34,
				.reg_data = { 0x0000, 0x1000 },
			},
			.fet = {
				.reg_addr = 0x34,
				.reg_mask = 0x0100,
				.disconnect_val = 0x0100,
			},
		},
		.batt_info = {
			.voltage_max		= 13050,   /* mV */
			.voltage_normal		= 11400,   /* mV */
			.voltage_min		= 9000,    /* mV */
			.precharge_current	= 256,     /* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 73,
		},
	},
	/* Sunwoda L20D3PG2, Gauge IC: BQ40Z697A. */
	[BATTERY_SUNWODA] = {
		.fuel_gauge = {
			.manuf_name = "Sunwoda",
			.device_name = "L20D3PG2",
			.ship_mode = {
				.reg_addr = 0x34,
				.reg_data = { 0x0000, 0x1000 },
			},
			.fet = {
				.reg_addr = 0x34,
				.reg_mask = 0x0100,
				.disconnect_val = 0x0100,
			},
		},
		.batt_info = {
			.voltage_max		= 13050, /* mV */
			.voltage_normal		= 11250, /* mV */
			.voltage_min		= 9000,  /* mV */
			.precharge_current	= 200,	 /* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 60,
		},
	},

	/* SIMPLO L20M3PG2, Gauge IC: BQ40Z697A. */
	[BATTERY_SMP] = {
		.fuel_gauge = {
			.manuf_name = "SMP",
			.device_name = "L20M3PG2",
			.ship_mode = {
				.reg_addr = 0x34,
				.reg_data = { 0x0000, 0x1000 },
			},
			.fet = {
				.reg_addr = 0x34,
				.reg_mask = 0x0100,
				.disconnect_val = 0x0100,
			},
		},
		.batt_info = {
			.voltage_max		= 13050, /* mV */
			.voltage_normal		= 11250, /* mV */
			.voltage_min		= 9000,  /* mV */
			.precharge_current	= 256,   /* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 45,
			.charging_min_c		= 0,
			.charging_max_c		= 45,
			.discharging_min_c	= -40,
			.discharging_max_c	= 73,
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_LGC;

enum battery_present battery_hw_present(void)
{
	return gpio_get_level(GPIO_EC_BATT_PRES_ODL) ? BP_NO : BP_YES;
}
