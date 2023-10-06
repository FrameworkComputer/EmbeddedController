/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery_fuel_gauge.h"
#include "charge_state.h"
#include "common.h"

/*
 * Battery info for all drawcia battery types. Note that the fields
 * start_charging_min/max and charging_min/max are not used for the charger.
 * The effective temperature limits are given by discharging_min/max_c.
 *
 * Fuel Gauge (FG) parameters which are used for determining if the battery
 * is connected, the appropriate ship mode (battery cutoff) command, and the
 * charge/discharge FETs status.
 *
 * Ship mode (battery cutoff) requires 2 writes to the appropriate smart battery
 * register. For some batteries, the charge/discharge FET bits are set when
 * charging/discharging is active, in other types, these bits set mean that
 * charging/discharging is disabled. Therefore, in addition to the mask for
 * these bits, a disconnect value must be specified. Note that for TI fuel
 * gauge, the charge/discharge FET status is found in Operation Status (0x54),
 * but a read of Manufacturer Access (0x00) will return the lower 16 bits of
 * Operation status which contains the FET status bits.
 *
 * The assumption for battery types supported is that the charge/discharge FET
 * status can be read with a sb_read() command and therefore, only the register
 * address, mask, and disconnect value need to be provided.
 */
const struct board_batt_params board_battery_info[] = {
	/* DynaPack CosMX Battery Information */
	[BATTERY_DYNAPACK_COS] = {
		.fuel_gauge = {
			.manuf_name = "333-2C-DA-A",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x0006,
				.disconnect_val = 0x0,
				.cfet_mask = 0x0004,
				.cfet_off_val = 0x0,
			},
			.flags = FUEL_GAUGE_FLAG_MFGACC,
		},
		.batt_info = {
			.voltage_max = 8800,	/* mV */
			.voltage_normal = 7700,
			.voltage_min = 6000,
			.precharge_current = 256,	/* mA */
			.start_charging_min_c = 0,
			.start_charging_max_c = 45,
			.charging_min_c = 0,
			.charging_max_c = 45,
			.discharging_min_c = -10,
			.discharging_max_c = 60,
		},
	},

	/* DynaPack ATL Battery Information */
	[BATTERY_DYNAPACK_ATL] = {
		.fuel_gauge = {
			.manuf_name = "333-27-DA-A",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x0006,
				.disconnect_val = 0x0,
				.cfet_mask = 0x0004,
				.cfet_off_val = 0x0,
			},
			.flags = FUEL_GAUGE_FLAG_MFGACC,
		},
		.batt_info = {
			.voltage_max = 8800,	/* mV */
			.voltage_normal = 7700,
			.voltage_min = 6000,
			.precharge_current = 256,	/* mA */
			.start_charging_min_c = 0,
			.start_charging_max_c = 45,
			.charging_min_c = 0,
			.charging_max_c = 45,
			.discharging_min_c = -10,
			.discharging_max_c = 60,
		},
	},

	/* DynaPack HIGHPOWER Battery Information */
	[BATTERY_DYNAPACK_HIGHPOWER] = {
		.fuel_gauge = {
			.manuf_name = "333-2D-0D-A",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x0006,
				.disconnect_val = 0x0,
				.cfet_mask = 0x0004,
				.cfet_off_val = 0x0,
			},
			.flags = FUEL_GAUGE_FLAG_MFGACC,
		},
		.batt_info = {
			.voltage_max = 8800,	/* mV */
			.voltage_normal = 7700,
			.voltage_min = 6000,
			.precharge_current = 256,	/* mA */
			.start_charging_min_c = 0,
			.start_charging_max_c = 45,
			.charging_min_c = 0,
			.charging_max_c = 45,
			.discharging_min_c = -10,
			.discharging_max_c = 60,
		},
	},

	/* DynaPack BYD Battery Information */
	[BATTERY_DYNAPACK_BYD] = {
		.fuel_gauge = {
			.manuf_name = "333-2E-0D-A",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x0006,
				.disconnect_val = 0x0,
				.cfet_mask = 0x0004,
				.cfet_off_val = 0x0,
			},
			.flags = FUEL_GAUGE_FLAG_MFGACC,
		},
		.batt_info = {
			.voltage_max = 8800,	/* mV */
			.voltage_normal = 7700,
			.voltage_min = 6000,
			.precharge_current = 256,	/* mA */
			.start_charging_min_c = 0,
			.start_charging_max_c = 45,
			.charging_min_c = 0,
			.charging_max_c = 45,
			.discharging_min_c = -10,
			.discharging_max_c = 60,
		},
	},

	/* Samsung SDI Battery Information */
	[BATTERY_SAMSUNG_SDI] = {
		.fuel_gauge = {
			.manuf_name = "333-54-DA-A",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x0006,
				.disconnect_val = 0x0,
				.cfet_mask = 0x0004,
				.cfet_off_val = 0x0,
			},
			.flags = FUEL_GAUGE_FLAG_MFGACC,
		},
		.batt_info = {
			.voltage_max = 8800,	/* mV */
			.voltage_normal = 7700,
			.voltage_min = 6000,
			.precharge_current = 256,	/* mA */
			.start_charging_min_c = 0,
			.start_charging_max_c = 45,
			.charging_min_c = 0,
			.charging_max_c = 45,
			.discharging_min_c = -10,
			.discharging_max_c = 60,
		},
	},

	/* Simplo CosMX Battery Information */
	[BATTERY_SIMPLO_COS] = {
		.fuel_gauge = {
			.manuf_name = "333-1C-DA-A",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x0006,
				.disconnect_val = 0x0,
				.cfet_mask = 0x0004,
				.cfet_off_val = 0x0,
			},
			.flags = FUEL_GAUGE_FLAG_MFGACC,
		},
		.batt_info = {
			.voltage_max = 8800,	/* mV */
			.voltage_normal = 7700,
			.voltage_min = 6000,
			.precharge_current = 256,	/* mA */
			.start_charging_min_c = 0,
			.start_charging_max_c = 45,
			.charging_min_c = 0,
			.charging_max_c = 45,
			.discharging_min_c = -10,
			.discharging_max_c = 60,
		},
	},

	/* Simplo HIGHPOWER Battery Information */
	[BATTERY_SIMPLO_HIGHPOWER] = {
		.fuel_gauge = {
			.manuf_name = "333-1D-DA-A",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x0006,
				.disconnect_val = 0x0,
				.cfet_mask = 0x0004,
				.cfet_off_val = 0x0,
			},
			.flags = FUEL_GAUGE_FLAG_MFGACC,
		},
		.batt_info = {
			.voltage_max = 8800,	/* mV */
			.voltage_normal = 7700,
			.voltage_min = 6000,
			.precharge_current = 256,	/* mA */
			.start_charging_min_c = 0,
			.start_charging_max_c = 45,
			.charging_min_c = 0,
			.charging_max_c = 45,
			.discharging_min_c = -10,
			.discharging_max_c = 60,
		},
	},

	/* CosMX Battery Information */
	[BATTERY_COS] = {
		.fuel_gauge = {
			.manuf_name = "333-AC-0D-A",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x0006,
				.disconnect_val = 0x0,
				.cfet_mask = 0x0004,
				.cfet_off_val = 0x0,
			},
			.flags = FUEL_GAUGE_FLAG_MFGACC,
		},
		.batt_info = {
			.voltage_max = 8800,	/* mV */
			.voltage_normal = 7700,
			.voltage_min = 6000,
			.precharge_current = 256,	/* mA */
			.start_charging_min_c = 0,
			.start_charging_max_c = 45,
			.charging_min_c = 0,
			.charging_max_c = 45,
			.discharging_min_c = -10,
			.discharging_max_c = 60,
		},
	},

	/* CosMX B00C4473A9D0002 Battery Information */
	[BATTERY_COS_2] = {
		.fuel_gauge = {
			.manuf_name = "333-AC-DA-A",
			.ship_mode = {
				.reg_addr = 0x0,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x0006,
				.disconnect_val = 0x0,
				.cfet_mask = 0x0004,
				.cfet_off_val = 0x0,
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
		},
	},

	/* ATL GB-S20-4473A9-01H&020H Battery Information
	 * Gauge IC : RAJ240045
	 */
	[BATTERY_ATL] = {
		.fuel_gauge = {
			.manuf_name = "313-B7-0D-A",
			.ship_mode = {
				.reg_addr = 0x0,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = 0x43,
				.reg_mask = 0x0003,
				.disconnect_val = 0x0,
				.cfet_mask = 0x0002,
				.cfet_off_val = 0x0,
			},
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
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_DYNAPACK_COS;
