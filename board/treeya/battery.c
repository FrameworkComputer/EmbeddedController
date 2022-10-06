/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery_fuel_gauge.h"
#include "common.h"
#include "util.h"

/*
 * Battery info for all Treeya battery types. Note that the fields
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
	/* SMP 5B10Q13163 */
	[BATTERY_SMP] = {
		.fuel_gauge = {
			.manuf_name = "SMP",
			.device_name = "L17M3PB0",
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
			.voltage_max		= 13050, /* mV */
			.voltage_normal		= 11250, /* mV */
			.voltage_min		= 9000,  /* mV */
			.precharge_current	= 186,	 /* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 60,
		},
	},
	/* LGC 5B10Q13162  */
	[BATTERY_LGC] = {
		.fuel_gauge = {
			.manuf_name = "LGC",
			.device_name = "L17L3PB0",
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
			.voltage_max		= 13050, /* mV */
			.voltage_normal		= 11400, /* mV */
			.voltage_min		= 9000,  /* mV */
			.precharge_current	= 181,	 /* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 73,
		},
	},
	/* Sunwoda L18D3PG1  */
	[BATTERY_SUNWODA] = {
		.fuel_gauge = {
			.manuf_name = "SUNWODA",
			.device_name = "L18D3PG1",
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
			.voltage_max		= 13050, /* mV */
			.voltage_normal		= 11250, /* mV */
			.voltage_min		= 9000,  /* mV */
			.precharge_current	= 200,	 /* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 60,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 60,
		},
	},

	/* SMP L19M3PG1 */
	[BATTERY_SMP_1] = {
		.fuel_gauge = {
			.manuf_name = "SMP",
			.device_name = "L19M3PG1",
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
			.voltage_max		= 13200, /* mV */
			.voltage_normal		= 11520, /* mV */
			.voltage_min		= 9000,  /* mV */
			.precharge_current	= 200,	 /* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 60,
			.charging_min_c		= 0,
			.charging_max_c		= 50,
			.discharging_min_c	= -20,
			.discharging_max_c	= 73,
		},
	},

	/* LGC  L19L3PG1 */
	[BATTERY_LGC_1] = {
		.fuel_gauge = {
			.manuf_name = "LGC",
			.device_name = "L19L3PG1",
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
			.voltage_max		= 13200, /* mV */
			.voltage_normal		= 11550, /* mV */
			.voltage_min		= 9000,  /* mV */
			.precharge_current	= 200,	 /* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 60,
			.charging_min_c		= 0,
			.charging_max_c		= 50,
			.discharging_min_c	= -20,
			.discharging_max_c	= 73,
		},
	},

	/* Celxpert  L19C3PG1 */
	[BATTERY_CEL_1] = {
		.fuel_gauge = {
			.manuf_name = "Celxpert",
			.device_name = "L19C3PG1",
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
			.voltage_max		= 13200, /* mV */
			.voltage_normal		= 11520, /* mV */
			.voltage_min		= 9000,  /* mV */
			.precharge_current	= 200,	 /* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 60,
			.charging_min_c		= 0,
			.charging_max_c		= 50,
			.discharging_min_c	= -20,
			.discharging_max_c	= 70,
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_SMP_1;
