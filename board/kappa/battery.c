/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "gpio.h"

const struct batt_conf_embed board_battery_info[] = {
	/* Dynapack HIGHPOWER DAK124960-W110703HT Battery Information */
	[BATTERY_DYNAPACK_HIGHPOWER] = {
		.manuf_name = "333-2D-14-A",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x0,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x0,
					.reg_mask = 0x0006,
					.disconnect_val = 0x0,
				},
				.flags = FUEL_GAUGE_FLAG_MFGACC,
			},
			.batt_info = {
				.voltage_max = 8700,		/* mV */
				.voltage_normal = 7600,		/* mV */
				.voltage_min = 6000,		/* mV */
				.precharge_current = 256,	/* mA */
				.start_charging_min_c = 0,
				.start_charging_max_c = 45,
				.charging_min_c = 0,
				.charging_max_c = 45,
				.discharging_min_c = -10,
				.discharging_max_c = 60,
				.vendor_param_start = 0x70,
			},
		},
	},
	/* Dynapack CosMX DAK124960-W0P0707HT Battery Information */
	[BATTERY_DYNAPACK_COS] = {
		.manuf_name = "333-2C-14-A",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x0,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x0,
					.reg_mask = 0x0006,
					.disconnect_val = 0x0,
				},
				.flags = FUEL_GAUGE_FLAG_MFGACC,
			},
			.batt_info = {
				.voltage_max = 8700,		/* mV */
				.voltage_normal = 7600,		/* mV */
				.voltage_min = 6000,		/* mV */
				.precharge_current = 256,	/* mA */
				.start_charging_min_c = 0,
				.start_charging_max_c = 45,
				.charging_min_c = 0,
				.charging_max_c = 45,
				.discharging_min_c = -10,
				.discharging_max_c = 60,
				.vendor_param_start = 0x70,
			},
		},
	},
	/* LGC MPPHPPFO021C Battery Information, BMU RAJ240045 */
	[BATTERY_LGC] = {
		.manuf_name = "313-42-14-A",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x0,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x43,
					.reg_mask = 0x0003,
					.disconnect_val = 0x0,
				},
			},
			.batt_info = {
				.voltage_max = 8700,		/* mV */
				.voltage_normal = 7520,		/* mV */
				.voltage_min = 6000,		/* mV */
				.precharge_current = 256,	/* mA */
				.start_charging_min_c = 0,
				.start_charging_max_c = 45,
				.charging_min_c = 0,
				.charging_max_c = 45,
				.discharging_min_c = -10,
				.discharging_max_c = 60,
				.vendor_param_start = 0x70,
			},
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_DYNAPACK_HIGHPOWER;

enum battery_present battery_hw_present(void)
{
	return gpio_get_level(GPIO_EC_BATT_PRES_ODL) ? BP_NO : BP_YES;
}
