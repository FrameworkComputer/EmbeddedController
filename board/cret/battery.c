/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery_fuel_gauge.h"
#include "battery_smart.h"
#include "charge_state.h"
#include "common.h"
#include "util.h"

/*
 * Battery info for all waddledoo battery types. Note that the fields
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
	/* BYD Battery Information */
	[BATTERY_BYD_1VX1H] = {
		.fuel_gauge = {
			.manuf_name = "BYD",
			.device_name = "DELL 1VX1H",
			.ship_mode = {
				.reg_addr = 0x44,
				.reg_data = { 0x0010, 0x0010 },
			},
			.flags = FUEL_GAUGE_FLAG_WRITE_BLOCK,
			.fet = {
				.reg_addr       = 0x0,
				.reg_mask       = 0x8000,
				.disconnect_val = 0x8000,
				.cfet_mask = 0x4000,
				.cfet_off_val = 0x4000,
			},
		},
		.batt_info = {
			.voltage_max          = 13200,    /* mV */
			.voltage_normal       = 11400,
			.voltage_min          = 9000,
			.precharge_current    = 256,       /* mA */
			.start_charging_min_c = -3,
			.start_charging_max_c = 50,
			.charging_min_c       = -3,
			.charging_max_c       = 60,
			.discharging_min_c    = -5,
			.discharging_max_c    = 70,
		},
	},

	/* BYD Battery Information */
	[BATTERY_BYD_YT39X] = {
		.fuel_gauge = {
			.manuf_name = "BYD",
			.device_name = "DELL YT39X",
			.ship_mode = {
				.reg_addr = 0x44,
				.reg_data = { 0x0010, 0x0010 },
			},
			.flags = FUEL_GAUGE_FLAG_WRITE_BLOCK,
			.fet = {
				.reg_addr       = 0x0,
				.reg_mask       = 0x8000,
				.disconnect_val = 0x8000,
				.cfet_mask = 0x4000,
				.cfet_off_val = 0x4000,
			},
		},
		.batt_info = {
			.voltage_max          = 13200,    /* mV */
			.voltage_normal       = 11400,
			.voltage_min          = 9000,
			.precharge_current    = 256,       /* mA */
			.start_charging_min_c = -3,
			.start_charging_max_c = 50,
			.charging_min_c       = -3,
			.charging_max_c       = 60,
			.discharging_min_c    = -5,
			.discharging_max_c    = 70,
		},
	},

	/* BYD Battery Information */
	[BATTERY_BYD_X0Y5M] = {
		.fuel_gauge = {
			.manuf_name = "BYD",
			.device_name = "DELL X0Y5M",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr       = 0x043,
				.reg_mask       = 0x0001,
				.disconnect_val = 0x0000,
				.cfet_mask = 0x0002,
				.cfet_off_val = 0x0000,
			},
		},
		.batt_info = {
			.voltage_max          = 13200,    /* mV */
			.voltage_normal       = 11400,
			.voltage_min          = 9000,
			.precharge_current    = 256,       /* mA */
			.start_charging_min_c = -3,
			.start_charging_max_c = 50,
			.charging_min_c       = -3,
			.charging_max_c       = 60,
			.discharging_min_c    = -5,
			.discharging_max_c    = 70,
		},
	},

	/* LGC Battery Information */
	[BATTERY_LGC_FDRHM] = {
		.fuel_gauge = {
			.manuf_name = "LGC-LGC3.65",
			.device_name = "DELL FDRHM",
			.ship_mode = {
				.reg_addr = 0x44,
				.reg_data = { 0x0010, 0x0010 },
			},
			.flags = FUEL_GAUGE_FLAG_WRITE_BLOCK,
			.fet = {
				.reg_addr       = 0x0,
				.reg_mask       = 0x8000,
				.disconnect_val = 0x8000,
				.cfet_mask = 0x4000,
				.cfet_off_val = 0x4000,
			},
		},
		.batt_info = {
			.voltage_max          = 13200,    /* mV */
			.voltage_normal       = 11460,
			.voltage_min          = 9000,
			.precharge_current    = 256,       /* mA */
			.start_charging_min_c = -3,
			.start_charging_max_c = 50,
			.charging_min_c       = -3,
			.charging_max_c       = 60,
			.discharging_min_c    = -5,
			.discharging_max_c    = 70,
		},
	},

	/* LGC Battery Information */
	[BATTERY_LGC_8GHCX] = {
		.fuel_gauge = {
			.manuf_name = "LGC-LGC3.65",
			.device_name = "DELL 8GHCX",
			.ship_mode = {
				.reg_addr = 0x44,
				.reg_data = { 0x0010, 0x0010 },
			},
			.flags = FUEL_GAUGE_FLAG_WRITE_BLOCK,
			.fet = {
				.reg_addr       = 0x0,
				.reg_mask       = 0x8000,
				.disconnect_val = 0x8000,
				.cfet_mask = 0x4000,
				.cfet_off_val = 0x4000,
			},
		},
		.batt_info = {
			.voltage_max          = 13200,    /* mV */
			.voltage_normal       = 11460,
			.voltage_min          = 9000,
			.precharge_current    = 256,       /* mA */
			.start_charging_min_c = -3,
			.start_charging_max_c = 50,
			.charging_min_c       = -3,
			.charging_max_c       = 60,
			.discharging_min_c    = -5,
			.discharging_max_c    = 70,
		},
	},


	/* SWD-ATL Battery Information */
	[BATTERY_SWD_ATL_WJPC4] = {
		.fuel_gauge = {
			.manuf_name = "SWD-ATL3.618",
			.device_name = "DELL WJPC4",
			.ship_mode = {
				.reg_addr = 0x44,
				.reg_data = { 0x0010, 0x0010 },
			},
			.flags = FUEL_GAUGE_FLAG_WRITE_BLOCK,
			.fet = {
				.reg_addr       = 0x0,
				.reg_mask       = 0x8000,
				.disconnect_val = 0x8000,
				.cfet_mask = 0x4000,
				.cfet_off_val = 0x4000,
			},
		},
		.batt_info = {
			.voltage_max          = 13200,    /* mV */
			.voltage_normal       = 11400,
			.voltage_min          = 9000,
			.precharge_current    = 256,       /* mA */
			.start_charging_min_c = -3,
			.start_charging_max_c = 50,
			.charging_min_c       = -3,
			.charging_max_c       = 60,
			.discharging_min_c    = -5,
			.discharging_max_c    = 70,
		},
	},

	/* SWD-ATL Battery Information */
	[BATTERY_SWD_ATL_CTGKT] = {
		.fuel_gauge = {
			.manuf_name = "SWD-ATL3.618",
			.device_name = "DELL CTGKT",
			.ship_mode = {
				.reg_addr = 0x44,
				.reg_data = { 0x0010, 0x0010 },
			},
			.flags = FUEL_GAUGE_FLAG_WRITE_BLOCK,
			.fet = {
				.reg_addr       = 0x0,
				.reg_mask       = 0x8000,
				.disconnect_val = 0x8000,
				.cfet_mask = 0x4000,
				.cfet_off_val = 0x4000,
			},
		},
		.batt_info = {
			.voltage_max          = 13200,    /* mV */
			.voltage_normal       = 11400,
			.voltage_min          = 9000,
			.precharge_current    = 256,       /* mA */
			.start_charging_min_c = -3,
			.start_charging_max_c = 50,
			.charging_min_c       = -3,
			.charging_max_c       = 60,
			.discharging_min_c    = -5,
			.discharging_max_c    = 70,
		},
	},

	/* SWD-COS Battery Information */
	[BATTERY_SWD_COS_WJPC4] = {
		.fuel_gauge = {
			.manuf_name = "SWD-COS3.634",
			.device_name = "DELL WJPC4",
			.ship_mode = {
				.reg_addr = 0x44,
				.reg_data = { 0x0010, 0x0010 },
			},
			.flags = FUEL_GAUGE_FLAG_WRITE_BLOCK,
			.fet = {
				.reg_addr       = 0x0,
				.reg_mask       = 0x8000,
				.disconnect_val = 0x8000,
				.cfet_mask = 0x4000,
				.cfet_off_val = 0x4000,
			},
		},
		.batt_info = {
			.voltage_max          = 13200,    /* mV */
			.voltage_normal       = 11400,
			.voltage_min          = 9000,
			.precharge_current    = 256,       /* mA */
			.start_charging_min_c = -3,
			.start_charging_max_c = 50,
			.charging_min_c       = -3,
			.charging_max_c       = 60,
			.discharging_min_c    = -5,
			.discharging_max_c    = 70,
		},
	},

	/* SWD-COS Battery Information */
	[BATTERY_SWD_COS_CTGKT] = {
		.fuel_gauge = {
			.manuf_name = "SWD-COS3.634",
			.device_name = "DELL CTGKT",
			.ship_mode = {
				.reg_addr = 0x44,
				.reg_data = { 0x0010, 0x0010 },
			},
			.flags = FUEL_GAUGE_FLAG_WRITE_BLOCK,
			.fet = {
				.reg_addr       = 0x0,
				.reg_mask       = 0x8000,
				.disconnect_val = 0x8000,
				.cfet_mask = 0x4000,
				.cfet_off_val = 0x4000,
			},
		},
		.batt_info = {
			.voltage_max          = 13200,    /* mV */
			.voltage_normal       = 11400,
			.voltage_min          = 9000,
			.precharge_current    = 256,       /* mA */
			.start_charging_min_c = -3,
			.start_charging_max_c = 50,
			.charging_min_c       = -3,
			.charging_max_c       = 60,
			.discharging_min_c    = -5,
			.discharging_max_c    = 70,
		},
	},

	/* SMP-ATL Battery Information */
	[BATTERY_SMP_ATL_VM732] = {
		.fuel_gauge = {
			.manuf_name = "SMP-ATL-3.61",
			.device_name = "DELL VM732",
			.ship_mode = {
				.reg_addr = 0x44,
				.reg_data = { 0x0010, 0x0010 },
			},
			.flags = FUEL_GAUGE_FLAG_WRITE_BLOCK,
			.fet = {
				.reg_addr       = 0x0,
				.reg_mask       = 0x8000,
				.disconnect_val = 0x8000,
				.cfet_mask = 0x4000,
				.cfet_off_val = 0x4000,
			},
		},
		.batt_info = {
			.voltage_max          = 13200,    /* mV */
			.voltage_normal       = 11400,
			.voltage_min          = 9000,
			.precharge_current    = 256,       /* mA */
			.start_charging_min_c = -3,
			.start_charging_max_c = 50,
			.charging_min_c       = -3,
			.charging_max_c       = 60,
			.discharging_min_c    = -5,
			.discharging_max_c    = 70,
		},
	},

	/* SMP-ATL Battery Information */
	[BATTERY_SMP_ATL_26JGK] = {
		.fuel_gauge = {
			.manuf_name = "SMP-ATL-3.61",
			.device_name = "DELL 26JGK",
			.ship_mode = {
				.reg_addr = 0x44,
				.reg_data = { 0x0010, 0x0010 },
			},
			.flags = FUEL_GAUGE_FLAG_WRITE_BLOCK,
			.fet = {
				.reg_addr       = 0x0,
				.reg_mask       = 0x8000,
				.disconnect_val = 0x8000,
				.cfet_mask = 0x4000,
				.cfet_off_val = 0x4000,
			},
		},
		.batt_info = {
			.voltage_max          = 13200,    /* mV */
			.voltage_normal       = 11400,
			.voltage_min          = 9000,
			.precharge_current    = 256,       /* mA */
			.start_charging_min_c = -3,
			.start_charging_max_c = 50,
			.charging_min_c       = -3,
			.charging_max_c       = 60,
			.discharging_min_c    = -5,
			.discharging_max_c    = 70,
		},
	},

	/* SMP-ATL Battery Information */
	[BATTERY_SMP_ATL_RF9H3] = {
		.fuel_gauge = {
			.manuf_name = "SMP-ATL-3.61",
			.device_name = "DELL RF9H3",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr       = 0x43,
				.reg_mask       = 0x0001,
				.disconnect_val = 0x0000,
				.cfet_mask = 0x0002,
				.cfet_off_val = 0x0000,
			},
		},
		.batt_info = {
			.voltage_max          = 13200,    /* mV */
			.voltage_normal       = 11400,
			.voltage_min          = 9000,
			.precharge_current    = 256,       /* mA */
			.start_charging_min_c = -3,
			.start_charging_max_c = 50,
			.charging_min_c       = -3,
			.charging_max_c       = 60,
			.discharging_min_c    = -5,
			.discharging_max_c    = 70,
		},
	},

	/* SMP-COS Battery Information */
	[BATTERY_SMP_COS_VM732] = {
		.fuel_gauge = {
			.manuf_name = "SMP-COS3.63",
			.device_name = "DELL VM732",
			.ship_mode = {
				.reg_addr = 0x44,
				.reg_data = { 0x0010, 0x0010 },
			},
			.flags = FUEL_GAUGE_FLAG_WRITE_BLOCK,
			.fet = {
				.reg_addr       = 0x0,
				.reg_mask       = 0x8000,
				.disconnect_val = 0x8000,
				.cfet_mask = 0x4000,
				.cfet_off_val = 0x4000,
			},
		},
		.batt_info = {
			.voltage_max          = 13200,    /* mV */
			.voltage_normal       = 11400,
			.voltage_min          = 9000,
			.precharge_current    = 256,       /* mA */
			.start_charging_min_c = -3,
			.start_charging_max_c = 50,
			.charging_min_c       = -3,
			.charging_max_c       = 60,
			.discharging_min_c    = -5,
			.discharging_max_c    = 70,
		},
	},

	/* SMP-COS Battery Information */
	[BATTERY_SMP_COS_26JGK] = {
		.fuel_gauge = {
			.manuf_name = "SMP-COS3.63",
			.device_name = "DELL 26JGK",
			.ship_mode = {
				.reg_addr = 0x44,
				.reg_data = { 0x0010, 0x0010 },
			},
			.flags = FUEL_GAUGE_FLAG_WRITE_BLOCK,
			.fet = {
				.reg_addr       = 0x0,
				.reg_mask       = 0x8000,
				.disconnect_val = 0x8000,
				.cfet_mask = 0x4000,
				.cfet_off_val = 0x4000,
			},
		},
		.batt_info = {
			.voltage_max          = 13200,    /* mV */
			.voltage_normal       = 11400,
			.voltage_min          = 9000,
			.precharge_current    = 256,       /* mA */
			.start_charging_min_c = -3,
			.start_charging_max_c = 50,
			.charging_min_c       = -3,
			.charging_max_c       = 60,
			.discharging_min_c    = -5,
			.discharging_max_c    = 70,
		},
	},
	/* SMP-COS Battery Information */
	[BATTERY_SMP_COS_RF9H3] = {
		.fuel_gauge = {
			.manuf_name = "SMP-COS3.63",
			.device_name = "DELL RF9H3",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr       = 0x43,
				.reg_mask       = 0x0001,
				.disconnect_val = 0x0000,
				.cfet_mask = 0x0002,
				.cfet_off_val = 0x0000,
			},
		},
		.batt_info = {
			.voltage_max          = 13200,    /* mV */
			.voltage_normal       = 11400,
			.voltage_min          = 9000,
			.precharge_current    = 256,       /* mA */
			.start_charging_min_c = -3,
			.start_charging_max_c = 50,
			.charging_min_c       = -3,
			.charging_max_c       = 60,
			.discharging_min_c    = -5,
			.discharging_max_c    = 70,
		},
	},

	/* SMP-HPT Battery Information */
	[BATTERY_SMP_HPT_RF9H3] = {
		.fuel_gauge = {
			.manuf_name = "SMP-HPT-3.65",
			.device_name = "DELL RF9H3",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr       = 0x043,
				.reg_mask       = 0x0001,
				.disconnect_val = 0x0000,
				.cfet_mask = 0x0002,
				.cfet_off_val = 0x0000,
			}
		},
		.batt_info = {
			.voltage_max          = 13200,    /* mV */
			.voltage_normal       = 11400,
			.voltage_min          = 9000,
			.precharge_current    = 256,       /* mA */
			.start_charging_min_c = -3,
			.start_charging_max_c = 50,
			.charging_min_c       = -3,
			.charging_max_c       = 60,
			.discharging_min_c    = -5,
			.discharging_max_c    = 70,
		},
	},

	/* BYD 16DPHYMD Battery Information */
	[BATTERY_BYD16] = {
		.fuel_gauge = {
			.manuf_name = "BYD-BYD3.685",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = 0x043,
				.reg_mask = 0x0001,
				.disconnect_val = 0x000,
				.cfet_mask = 0x0002,
				.cfet_off_val = 0x0000,
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

	/* LGC Battery Information */
	[BATTERY_LGC3] = {
		.fuel_gauge = {
			.manuf_name = "LGC-LGC3.553",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr       = 0x0,
				.reg_mask       = 0x8000,
				.disconnect_val = 0x8000,
				.cfet_mask = 0x4000,
				.cfet_off_val = 0x4000,
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

	/* SIMPLO Battery Information */
	[BATTERY_SIMPLO] = {
		.fuel_gauge = {
			.manuf_name = "SMP-SDI3.72",
			.ship_mode = {
				.reg_addr = 0x0,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = 0x043,
				.reg_mask = 0x0001,
				.disconnect_val = 0x000,
				.cfet_mask = 0x0002,
				.cfet_off_val = 0x0000,
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

	/* SIMPLO-LISHEN 7T0D3YMD Battery Information */
	[BATTERY_SIMPLO_LS] = {
		.fuel_gauge = {
			.manuf_name = "SMP-LS3.66",
			.ship_mode = {
				.reg_addr = 0x0,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = 0x043,
				.reg_mask = 0x0001,
				.disconnect_val = 0x000,
				.cfet_mask = 0x0002,
				.cfet_off_val = 0x0000,
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
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_BYD_1VX1H;

int charger_profile_override(struct charge_state_data *curr)
{
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		curr->requested_current =
			MIN(curr->requested_current, CHARGING_CURRENT_1100MA);
	}

	return 0;
}

enum ec_status charger_profile_override_get_param(uint32_t param,
						  uint32_t *value)
{
	return EC_RES_INVALID_PARAM;
}

enum ec_status charger_profile_override_set_param(uint32_t param,
						  uint32_t value)
{
	return EC_RES_INVALID_PARAM;
}
