/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery_fuel_gauge.h"
#include "common.h"
#include "util.h"

/*
 * Battery info for all homestar battery types. Note that the fields
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

const struct batt_conf_embed board_battery_info[] = {
	/* COSMX GH02047XL 333-1C-DA-A */
	[BATTERY_GH02047XL_1C] = {
		.manuf_name = "333-1C-DA-A",
		.device_name = "GH02047XL",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x0,
					.reg_mask = 0x0002,
					.disconnect_val = 0x0,
				},
				.flags = FUEL_GAUGE_FLAG_MFGACC,
			},
			.batt_info = {
				.voltage_max            = 8800, /* mV */
				.voltage_normal         = 7700, /* mV */
				.voltage_min            = 6000, /* mV */
				.precharge_current      = 256,  /* mA */
				.start_charging_min_c   = 0,
				.start_charging_max_c   = 45,
				.charging_min_c         = 0,
				.charging_max_c         = 45,
				.discharging_min_c      = -10,
				.discharging_max_c      = 60,
				.vendor_param_start     = 0x70,
			},
		},
	},
	/* COSMX GH02047XL */
	[BATTERY_GH02047XL] = {
		.manuf_name = "333-AC-DA-A",
		.device_name = "GH02047XL",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x0,
					.reg_mask = 0x0002,
					.disconnect_val = 0x0,
				},
				.flags = FUEL_GAUGE_FLAG_MFGACC,
			},
			.batt_info = {
				.voltage_max            = 8800, /* mV */
				.voltage_normal         = 7700, /* mV */
				.voltage_min            = 6000, /* mV */
				.precharge_current      = 256,  /* mA */
				.start_charging_min_c   = 0,
				.start_charging_max_c   = 45,
				.charging_min_c         = 0,
				.charging_max_c         = 45,
				.discharging_min_c      = -10,
				.discharging_max_c      = 60,
				.vendor_param_start     = 0x70,
			},
		},
	},
	/* COSMX DS02032XL */
	[BATTERY_DS02032XL] = {
		.manuf_name = "333-AC-13-A",
		.device_name = "DS02032XL",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x0,
					.reg_mask = 0x0002,
					.disconnect_val = 0x0,
				},
				.flags = FUEL_GAUGE_FLAG_MFGACC,
			},
			.batt_info = {
				.voltage_max            = 8800, /* mV */
				.voltage_normal         = 7700, /* mV */
				.voltage_min            = 6000, /* mV */
				.precharge_current      = 256,  /* mA */
				.start_charging_min_c   = 0,
				.start_charging_max_c   = 45,
				.charging_min_c         = 0,
				.charging_max_c         = 45,
				.discharging_min_c      = -10,
				.discharging_max_c      = 60,
				.vendor_param_start     = 0x70,
			},
		},
	},
	/* SMP DS02032XL */
	[BATTERY_DS02032XL_1C] = {
		.manuf_name = "333-1C-13-A",
		.device_name = "DS02032XL",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x0,
					.reg_mask = 0x0002,
					.disconnect_val = 0x0,
				},
				.flags = FUEL_GAUGE_FLAG_MFGACC,
			},
			.batt_info = {
				.voltage_max            = 8800, /* mV */
				.voltage_normal         = 7700, /* mV */
				.voltage_min            = 6000, /* mV */
				.precharge_current      = 256,  /* mA */
				.start_charging_min_c   = 0,
				.start_charging_max_c   = 45,
				.charging_min_c         = 0,
				.charging_max_c         = 45,
				.discharging_min_c      = -10,
				.discharging_max_c      = 60,
				.vendor_param_start     = 0x70,
			},
		},
	},
	/* Sunwoda L21D4PG0 */
	[BATTERY_L21D4PG0] = {
		.manuf_name = "Sunwoda",
		.device_name = "L21D4PG0",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x34,
					.reg_data = { 0x0000, 0x1000 },
				},
				.fet = {
					.reg_addr = 0x34,
					.reg_mask = 0x0100,
					.disconnect_val = 0x0,
				},
				.flags = FUEL_GAUGE_FLAG_MFGACC,
			},
			.batt_info = {
				.voltage_max            = 8900, /* mV */
				.voltage_normal         = 7720, /* mV */
				.voltage_min            = 6000, /* mV */
				.precharge_current      = 274,  /* mA */
				.start_charging_min_c   = 0,
				.start_charging_max_c   = 50,
				.charging_min_c         = 0,
				.charging_max_c         = 60,
				.discharging_min_c      = -20,
				.discharging_max_c      = 60,
				.vendor_param_start     = 0x2f,
			},
		},
	},
	/* SMP L21M4PG0 */
	[BATTERY_L21M4PG0] = {
		.manuf_name = "SMP",
		.device_name = "L21M4PG0",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x34,
					.reg_data = { 0x0000, 0x1000 },
				},
				.fet = {
					.reg_addr = 0x34,
					.reg_mask = 0x0100,
					.disconnect_val = 0x0,
				},
				.flags = FUEL_GAUGE_FLAG_MFGACC,
			},
			.batt_info = {
				.voltage_max            = 8900, /* mV */
				.voltage_normal         = 7720, /* mV */
				.voltage_min            = 6000, /* mV */
				.precharge_current      = 274,  /* mA */
				.start_charging_min_c   = 0,
				.start_charging_max_c   = 50,
				.charging_min_c         = 0,
				.charging_max_c         = 60,
				.discharging_min_c      = -20,
				.discharging_max_c      = 60,
				.vendor_param_start     = 0x2f,
			},
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_L21D4PG0;
