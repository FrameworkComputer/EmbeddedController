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
 * Battery info for all Dood battery types. Note that the fields
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
	/* LGC AC15A8J Battery Information */
	[BATTERY_LGC15] = {
		.fuel_gauge = {
			.manuf_name = "LGC",
			.device_name = "AC15A8J",
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
			.voltage_max		= 13200,
			.voltage_normal		= 11520, /* mV */
			.voltage_min		= 9000, /* mV */
			.precharge_current	= 256,	/* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= 0,
			.discharging_max_c	= 60,
		},
	},

	/* Panasonic AP1505L Battery Information */
	[BATTERY_PANASONIC_AP15O5L] = {
		.fuel_gauge = {
			.manuf_name = "PANASONIC",
			.device_name = "AP15O5L",
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
			.voltage_normal		= 11550, /* mV */
			.voltage_min		= 9000, /* mV */
			.precharge_current	= 256,	/* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= 0,
			.discharging_max_c	= 60,
		},
	},

	/* SANYO AC15A3J Battery Information */
	[BATTERY_SANYO] = {
		.fuel_gauge = {
			.manuf_name = "SANYO",
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
			.voltage_max		= TARGET_WITH_MARGIN(13200, 5),
			.voltage_normal		= 11550, /* mV */
			.voltage_min		= 9000, /* mV */
			.precharge_current	= 256,	/* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= 0,
			.discharging_max_c	= 60,
		},
	},

	/* Sony Ap13J4K Battery Information */
	[BATTERY_SONY] = {
		.fuel_gauge = {
			.manuf_name = "SONYCorp",
			.ship_mode = {
				.reg_addr = 0x3A,
				.reg_data = { 0xC574, 0xC574 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x8000,
				.disconnect_val = 0x8000,
			},
		},
		.batt_info = {
			.voltage_max		= TARGET_WITH_MARGIN(13200, 5),
			.voltage_normal		= 11400, /* mV */
			.voltage_min		= 9000, /* mV */
			.precharge_current	= 256,	/* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= 0,
			.discharging_max_c	= 60,
		},
	},

	/* Simplo AP13J7K Battery Information */
	[BATTERY_SMP_AP13J7K] = {
		.fuel_gauge = {
			.manuf_name = "SIMPLO",
			.device_name = "AP13J7K",
			.ship_mode = {
				.reg_addr = 0x3A,
				.reg_data = { 0xC574, 0xC574 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x0002,
				.disconnect_val = 0x0000,
			},
			.flags = FUEL_GAUGE_FLAG_MFGACC,
		},
		.batt_info = {
			.voltage_max		= 13050,
			.voltage_normal		= 11400, /* mV */
			.voltage_min		= 9000, /* mV */
			.precharge_current	= 256,	/* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 45,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= 0,
			.discharging_max_c	= 60,
		},
	},

	/* Panasonic AC15A3J Battery Information */
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
			.voltage_normal		= 11550, /* mV */
			.voltage_min		= 9000, /* mV */
			.precharge_current	= 256,	/* mA */
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
			.voltage_max		= 13050,
			.voltage_normal		= 11250,
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

	/* LGC AP19A8K Battery Information */
	[BATTERY_LGC_AP19A8K] = {
		.fuel_gauge = {
			.manuf_name = "LGC KTxxxxGxxx",
			.device_name = "AP19A8K",
			.ship_mode = {
				.reg_addr = 0x3A,
				.reg_data = { 0xC574, 0xC574 },
			},
			.fet = {
				.reg_addr = 0x43,
				.reg_mask = 0x0001,
				.disconnect_val = 0x0,
				.cfet_mask = 0x0002,
				.cfet_off_val = 0x0,
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

	/* LGC KT0030G023 Battery Information */
	[BATTERY_LGC_G023] = {
		.fuel_gauge = {
			.manuf_name = "LGC KT0030G023",
			.device_name = "AP19A8K",
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
			.voltage_max            = 13200,
			.voltage_normal         = 11550,
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

	/* Simplo PC-VP-BP44 Battery Information */
	[BATTERY_SMP_PCVPBP144] = {
		.fuel_gauge = {
			.manuf_name = "SIMPLO",
			.device_name = "PC-VP-BP144",
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
			.voltage_max		= 13050,
			.voltage_normal		= 11250, /* mV */
			.voltage_min		= 9000, /* mV */
			.precharge_current	= 128,	/* mA */
			.start_charging_min_c   = 0,
			.start_charging_max_c   = 45,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 60,
		},
	},

	/* Simplo PC-VP-BP126 Battery Information */
	[BATTERY_SMP_PCVPBP126] = {
		.fuel_gauge = {
			.manuf_name = "SIMPLO",
			.device_name = "PC-VP-BP126",
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
			.voltage_max		= 13200,
			.voltage_normal		= 11520, /* mV */
			.voltage_min		= 9000, /* mV */
			.precharge_current	= 128,	/* mA */
			.start_charging_min_c   = 0,
			.start_charging_max_c   = 45,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 60,
		},
	},

	/* Simplo PC-VP-BP136 Battery Information */
	[BATTERY_SMP_PCVPBP136] = {
		.fuel_gauge = {
			.manuf_name = "SIMPLO",
			.device_name = "PC-VP-BP136",
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
			.voltage_max		= 13200,
			.voltage_normal		= 11520, /* mV */
			.voltage_min		= 9000, /* mV */
			.precharge_current	= 128,	/* mA */
			.start_charging_min_c   = 0,
			.start_charging_max_c   = 45,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 60,
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_PANASONIC_AC15A3J;
