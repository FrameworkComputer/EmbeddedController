/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "gpio.h"

const struct batt_conf_embed board_battery_info[] = {
	/* Dynapack ATL Battery Information */
	[BATTERY_DYNAPACK_ATL] = {
		.manuf_name = "333-27-DA-A",
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
				.voltage_max = 8800,		/* mV */
				.voltage_normal = 7700,		/* mV */
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
	/* Dynapack CosMX Battery Information */
	[BATTERY_DYNAPACK_COS] = {
		.manuf_name = "333-2C-DA-A",
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
				.voltage_max = 8800,		/* mV */
				.voltage_normal = 7700,		/* mV */
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
	/* Simplo CosMX Battery Information */
	[BATTERY_SIMPLO_COS] = {
		.manuf_name = "333-1C-DA-A",
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
				.voltage_max = 8800,		/* mV */
				.voltage_normal = 7700,		/* mV */
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
	/* Simplo HIGHPOWER Battery Information */
	[BATTERY_SIMPLO_HIGHPOWER] = {
		.manuf_name = "333-1D-DA-A",
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
				.voltage_max = 8800,		/* mV */
				.voltage_normal = 7700,		/* mV */
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
	/* Samsung SDI Battery Information */
	[BATTERY_SAMSUNG_SDI] = {
		.manuf_name = "333-54-DA-A",
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
				.voltage_max = 8800,		/* mV */
				.voltage_normal = 7700,		/* mV */
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
	/* CosMX Battery Information */
	[BATTERY_COS] = {
		.manuf_name = "333-AC-DA-A",
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
				.voltage_max = 8800,		/* mV */
				.voltage_normal = 7700,		/* mV */
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
	/* DynaPack CosMX DAK125720-W0P0701HT Battery Information */
	[BATTERY_DYNAPACK_COS_44WH] = {
		.manuf_name = "333-2C-DB-A",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
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
				.voltage_max = 8800,		/* mV */
				.voltage_normal = 7700,		/* mV */
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
	/* DynaPack ATL DAK125720-W0G0701HT Battery Information */
	[BATTERY_DYNAPACK_ATL_44WH] = {
		.manuf_name = "333-27-DB-A",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
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
				.voltage_max = 8800,		/* mV */
				.voltage_normal = 7700,		/* mV */
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
	/* Simplo HIGHPOWER 996Q4242H Battery Information */
	[BATTERY_SIMPLO_HIGHPOWER_44WH] = {
		.manuf_name = "333-1D-DB-A",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
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
				.voltage_max = 8800,		/* mV */
				.voltage_normal = 7700,		/* mV */
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

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_DYNAPACK_ATL;

enum battery_present battery_hw_present(void)
{
	return gpio_get_level(GPIO_EC_BATT_PRES_ODL) ? BP_NO : BP_YES;
}
