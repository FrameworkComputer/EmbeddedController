/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery_fuel_gauge.h"
#include "common.h"
#include "util.h"

/*
 * Battery info for all fleex battery types. Note that the fields
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
	/* BYD Battery Information */
	[BATTERY_BYD] = {
		.manuf_name = "BYD",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x0,
					.reg_mask = 0x2000,
					.disconnect_val = 0x2000,
				},
			},
			.batt_info = {
				.voltage_max		= 13200, /* mV */
				.voltage_normal		= 11400, /* mV */
				.voltage_min		= 9000, /* mV */
				.precharge_current	= 256,	/* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 50,
				.charging_min_c		= 0,
				.charging_max_c		= 60,
				.discharging_min_c	= -20,
				.discharging_max_c	= 70,
			},
		},
	},

	/* BYD 16DPHYMD Battery Information */
	[BATTERY_BYD16] = {
		.manuf_name = "BYD-BYD3.685",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x043,
					.reg_mask = 0x0001,
					.disconnect_val = 0x000,
				},
			},
			.batt_info = {
				.voltage_max		= 13200, /* mV */
				.voltage_normal		= 11400, /* mV */
				.voltage_min		= 9000, /* mV */
				.precharge_current	= 256,	/* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 50,
				.charging_min_c		= 0,
				.charging_max_c		= 60,
				.discharging_min_c	= 0,
				.discharging_max_c	= 70,
			},
		},
	},

	/* LGC Battery Information */
	[BATTERY_LGC] = {
		.manuf_name = "LGC-LGC3.553",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x0,
					.reg_mask = 0x2000,
					.disconnect_val = 0x2000,
				},
			},
			.batt_info = {
				.voltage_max		= 13200, /* mV */
				.voltage_normal		= 11400, /* mV */
				.voltage_min		= 9000, /* mV */
				.precharge_current	= 256,	/* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 50,
				.charging_min_c		= 0,
				.charging_max_c		= 60,
				.discharging_min_c	= -20,
				.discharging_max_c	= 70,
			},
		},
	},

	/* LGC JPFMRYMD Battery Information */
	[BATTERY_LGC3] = {
		.manuf_name = "LGC-LGC3.685",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x44,
					.reg_data = { 0x0010, 0x0010 },
				},
				.flags = FUEL_GAUGE_FLAG_WRITE_BLOCK,
				.fet = {
					.reg_addr = 0x0,
					.reg_mask = 0x2000,
					.disconnect_val = 0x2000,
				},
			},
			.batt_info = {
				.voltage_max		= 13200, /* mV */
				.voltage_normal		= 11400, /* mV */
				.voltage_min		= 9000, /* mV */
				.precharge_current	= 256,	/* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 50,
				.charging_min_c		= 0,
				.charging_max_c		= 60,
				.discharging_min_c	= 0,
				.discharging_max_c	= 70,
			},
		},
	},

	/* SIMPLO Battery Information */
	[BATTERY_SIMPLO] = {
		.manuf_name = "SMP-SDI3.72",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x0,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x043,
					.reg_mask = 0x0001,
					.disconnect_val = 0x000,
				},
			},
			.batt_info = {
				.voltage_max		= 13200, /* mV */
				.voltage_normal		= 11400, /* mV */
				.voltage_min		= 9000, /* mV */
				.precharge_current	= 256,	/* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 50,
				.charging_min_c		= 0,
				.charging_max_c		= 60,
				.discharging_min_c	= -20,
				.discharging_max_c	= 70,
			},
		},
	},

	/* SIMPLO-ATL 7T0D3YMD Battery Information */
	[BATTERY_SIMPLO_ATL] = {
		.manuf_name = "SMP-ATL3.61",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x0,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x043,
					.reg_mask = 0x0001,
					.disconnect_val = 0x000,
				},
			},
			.batt_info = {
				.voltage_max		= 13200, /* mV */
				.voltage_normal		= 11400, /* mV */
				.voltage_min		= 9000, /* mV */
				.precharge_current	= 256,	/* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 50,
				.charging_min_c		= 0,
				.charging_max_c		= 60,
				.discharging_min_c	= 0,
				.discharging_max_c	= 70,
			},
		},
	},

	/* SIMPLO-LISHEN 7T0D3YMD Battery Information */
	[BATTERY_SIMPLO_LS] = {
		.manuf_name = "SMP-LS3.66",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x0,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x043,
					.reg_mask = 0x0001,
					.disconnect_val = 0x000,
				},
			},
			.batt_info = {
				.voltage_max		= 13200, /* mV */
				.voltage_normal		= 11400, /* mV */
				.voltage_min		= 9000, /* mV */
				.precharge_current	= 256,	/* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 50,
				.charging_min_c		= 0,
				.charging_max_c		= 60,
				.discharging_min_c	= 0,
				.discharging_max_c	= 70,
			},
		},
	},

	/* SIMPLO-COSMX 7T0D3YMD Battery Information */
	[BATTERY_SIMPLO_COS] = {
		.manuf_name = "SMP-COS3.63",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x0,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x043,
					.reg_mask = 0x0001,
					.disconnect_val = 0x000,
				},
			},
			.batt_info = {
				.voltage_max		= 13200, /* mV */
				.voltage_normal		= 11400, /* mV */
				.voltage_min		= 9000, /* mV */
				.precharge_current	= 256,	/* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 50,
				.charging_min_c		= 0,
				.charging_max_c		= 60,
				.discharging_min_c	= 0,
				.discharging_max_c	= 70,
			},
		},
	},

	/* SWD-ATL 65N6HYMD Battery Information */
	[BATTERY_SWD_ATL] = {
		.manuf_name = "SWD-ATL3.618",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x44,
					.reg_data = { 0x0010, 0x0010 },
				},
				.flags = FUEL_GAUGE_FLAG_WRITE_BLOCK,
				.fet = {
					.reg_addr = 0x0,
					.reg_mask = 0x2000,
					.disconnect_val = 0x2000,
				},
			},
			.batt_info = {
				.voltage_max		= 13200, /* mV */
				.voltage_normal		= 11400, /* mV */
				.voltage_min		= 9000, /* mV */
				.precharge_current	= 256,	/* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 50,
				.charging_min_c		= 0,
				.charging_max_c		= 60,
				.discharging_min_c	= 0,
				.discharging_max_c	= 70,
			},
		},
	},

	/* SWD-COSLIGHT 65N6HYMD Battery Information */
	[BATTERY_SWD_COS] = {
		.manuf_name = "SWD-COS3.634",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x44,
					.reg_data = { 0x0010, 0x0010 },
				},
				.flags = FUEL_GAUGE_FLAG_WRITE_BLOCK,
				.fet = {
					.reg_addr = 0x0,
					.reg_mask = 0x2000,
					.disconnect_val = 0x2000,
				},
			},
			.batt_info = {
				.voltage_max		= 13200, /* mV */
				.voltage_normal		= 11400, /* mV */
				.voltage_min		= 9000, /* mV */
				.precharge_current	= 256,	/* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 50,
				.charging_min_c		= 0,
				.charging_max_c		= 60,
				.discharging_min_c	= 0,
				.discharging_max_c	= 70,
			},
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_LGC;
