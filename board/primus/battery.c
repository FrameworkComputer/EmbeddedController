/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery_fuel_gauge.h"
#include "common.h"
#include "compile_time_macros.h"

/*
 * Battery info for all Primus battery types. Note that the fields
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
	[BATTERY_SUNWODA_5B11F21946] = {
		.manuf_name = "Sunwoda",
		.device_name = "LNV-5B11F21946",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x0000,
					.reg_mask = 0x6000,
					.disconnect_val = 0x6000,
				},
				.flags = FUEL_GAUGE_FLAG_MFGACC,
			},
			.batt_info = {
				.voltage_max		= 13200, /* mV */
				.voltage_normal		= 11520, /* mV */
				.voltage_min		= 9000,  /* mV */
				.precharge_current	= 251,	 /* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 50,
				.charging_min_c		= 0,
				.charging_max_c		= 60,
				.discharging_min_c	= -20,
				.discharging_max_c	= 60,
			},
		},
	},

	[BATTERY_SUNWODA_5B11H56342] = {
		.manuf_name = "Sunwoda",
		.device_name = "LNV-5B11H56342",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x0000,
					.reg_mask = 0x6000,
					.disconnect_val = 0x6000,
				},
				.flags = FUEL_GAUGE_FLAG_MFGACC,
			},
			.batt_info = {
				.voltage_max		= 13200, /* mV */
				.voltage_normal		= 11520, /* mV */
				.voltage_min		= 9000,  /* mV */
				.precharge_current	= 251,	 /* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 50,
				.charging_min_c		= 0,
				.charging_max_c		= 60,
				.discharging_min_c	= -20,
				.discharging_max_c	= 60,
			},
		},
	},

	[BATTERY_SMP_5B11F21953] = {
		.manuf_name = "SMP",
		.device_name = "LNV-5B11F21953",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x0000,
					.reg_mask = 0x6000,
					.disconnect_val = 0x6000,
				},
				.flags = FUEL_GAUGE_FLAG_MFGACC,
			},
			.batt_info = {
				.voltage_max		= 13200, /* mV */
				.voltage_normal		= 11520, /* mV */
				.voltage_min		= 9000,  /* mV */
				.precharge_current	= 250,	 /* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 60,
				.charging_min_c		= 0,
				.charging_max_c		= 60,
				.discharging_min_c	= -20,
				.discharging_max_c	= 60,
			},
		},
	},

	[BATTERY_SMP_5B11H56344] = {
		.manuf_name = "SMP",
		.device_name = "LNV-5B11H56344",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x0000,
					.reg_mask = 0x6000,
					.disconnect_val = 0x6000,
				},
				.flags = FUEL_GAUGE_FLAG_MFGACC,
			},
			.batt_info = {
				.voltage_max		= 13200, /* mV */
				.voltage_normal		= 11520, /* mV */
				.voltage_min		= 9000,  /* mV */
				.precharge_current	= 250,	 /* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 60,
				.charging_min_c		= 0,
				.charging_max_c		= 60,
				.discharging_min_c	= -20,
				.discharging_max_c	= 60,
			},
		},
	},

	[BATTERY_CELXPERT_5B11F21941] = {
		.manuf_name = "Celxpert",
		.device_name = "LNV-5B11F21941",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x0000,
					.reg_mask = 0x6000,
					.disconnect_val = 0x6000,
				},
				.flags = FUEL_GAUGE_FLAG_MFGACC,
			},
			.batt_info = {
				.voltage_max		= 13200, /* mV */
				.voltage_normal		= 11520, /* mV */
				.voltage_min		= 9000,  /* mV */
				.precharge_current	= 487,	 /* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 50,
				.charging_min_c		= 0,
				.charging_max_c		= 60,
				.discharging_min_c	= -20,
				.discharging_max_c	= 70,
			},
		},
	},

	[BATTERY_CELXPERT_5B11H56343] = {
		.manuf_name = "Celxpert",
		.device_name = "LNV-5B11H56343",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x0000,
					.reg_mask = 0x6000,
					.disconnect_val = 0x6000,
				},
				.flags = FUEL_GAUGE_FLAG_MFGACC,
			},
			.batt_info = {
				.voltage_max		= 13200, /* mV */
				.voltage_normal		= 11520, /* mV */
				.voltage_min		= 9000,  /* mV */
				.precharge_current	= 487,	 /* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 50,
				.charging_min_c		= 0,
				.charging_max_c		= 60,
				.discharging_min_c	= -20,
				.discharging_max_c	= 70,
			},
		},
	},

	[BATTERY_CELXPERT_5B11M90007] = {
		.manuf_name = "Celxpert",
		.device_name = "LNV-5B11M90007",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x0000,
					.reg_mask = 0x6000,
					.disconnect_val = 0x6000,
				},
				.flags = FUEL_GAUGE_FLAG_MFGACC,
			},
			.batt_info = {
				.voltage_max		= 13200, /* mV */
				.voltage_normal		= 11520, /* mV */
				.voltage_min		= 9000,  /* mV */
				.precharge_current	= 487,	 /* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 50,
				.charging_min_c		= 0,
				.charging_max_c		= 60,
				.discharging_min_c	= -20,
				.discharging_max_c	= 70,
			},
		},
	},

	[BATTERY_SMP_5B11M90006] = {
		.manuf_name = "SMP",
		.device_name = "LNV-5B11M90006",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x0000,
					.reg_mask = 0x6000,
					.disconnect_val = 0x6000,
				},
				.flags = FUEL_GAUGE_FLAG_MFGACC,
			},
			.batt_info = {
				.voltage_max		= 13200, /* mV */
				.voltage_normal		= 11520, /* mV */
				.voltage_min		= 9000,  /* mV */
				.precharge_current	= 250,	 /* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 60,
				.charging_min_c		= 0,
				.charging_max_c		= 60,
				.discharging_min_c	= -20,
				.discharging_max_c	= 60,
			},
		},
	},

	[BATTERY_SUNWODA_5B11M90008] = {
		.manuf_name = "Sunwoda",
		.device_name = "LNV-5B11M90008",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x0000,
					.reg_mask = 0x6000,
					.disconnect_val = 0x6000,
				},
				.flags = FUEL_GAUGE_FLAG_MFGACC,
			},
			.batt_info = {
				.voltage_max		= 13200, /* mV */
				.voltage_normal		= 11520, /* mV */
				.voltage_min		= 9000,  /* mV */
				.precharge_current	= 251,	 /* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 50,
				.charging_min_c		= 0,
				.charging_max_c		= 60,
				.discharging_min_c	= -20,
				.discharging_max_c	= 60,
			},
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_SUNWODA_5B11F21946;
