/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery_fuel_gauge.h"
#include "battery_smart.h"
#include "charge_state.h"
#include "common.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "util.h"

/*
 * Battery info for all Zork battery types. Note that the fields
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
	/* SMP L19M3PG1 */
	[BATTERY_SMP] = {
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
			},
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

	/* SMP L20M3PG1 57W
	 * Gauge IC: TI BQ40Z696A
	 */
	[BATTERY_SMP_1] = {
		.fuel_gauge = {
			.manuf_name = "SMP",
			.device_name = "L20M3PG1",
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
			.precharge_current	= 247,	 /* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 70,
		},
	},

	/* SMP L20M3PG0 47W
	 * Gauge IC: TI BQ40Z696A
	 */
	[BATTERY_SMP_2] = {
		.fuel_gauge = {
			.manuf_name = "SMP",
			.device_name = "L20M3PG0",
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
			.precharge_current	= 256,	 /* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 70,
		},
	},

	/* SMP L20M3PG3 47W
	 * Gauge IC: Renesas RAJ240047
	 */
	[BATTERY_SMP_3] = {
		.fuel_gauge = {
			.manuf_name = "SMP",
			.device_name = "L20M3PG3",
			.ship_mode = {
				.reg_addr = 0x34,
				.reg_data = { 0x0000, 0x1000 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x0010,
				.disconnect_val = 0x0,
			},
		},
		.batt_info = {
			.voltage_max            = 13200, /* mV */
			.voltage_normal         = 11520, /* mV */
			.voltage_min            = 9000,  /* mV */
			.precharge_current      = 256,   /* mA */
			.start_charging_min_c   = 0,
			.start_charging_max_c   = 50,
			.charging_min_c         = 0,
			.charging_max_c         = 60,
			.discharging_min_c      = -20,
			.discharging_max_c      = 70,
		},
	},

	/* LGC  L19L3PG1 */
	[BATTERY_LGC] = {
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
			},
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

	/* LGC L20L3PG1 57W
	 * Gauge IC: Renesas
	 */
	[BATTERY_LGC_1] = {
		.fuel_gauge = {
			.manuf_name = "LGC",
			.device_name = "L20L3PG1",
			.ship_mode = {
				.reg_addr = 0x34,
				.reg_data = { 0x0000, 0x1000 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x0010,
				.disconnect_val = 0x0,
			},
		},
		.batt_info = {
			.voltage_max		= 13200, /* mV */
			.voltage_normal		= 11580, /* mV */
			.voltage_min		= 9000,  /* mV */
			.precharge_current	= 256,	 /* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 70,
		},
	},

	/* LGC L20L3PG0 47W
	 * Gauge IC: Renesas
	 */
	[BATTERY_LGC_2] = {
		.fuel_gauge = {
			.manuf_name = "LGC",
			.device_name = "L20L3PG0",
			.ship_mode = {
				.reg_addr = 0x34,
				.reg_data = { 0x0000, 0x1000 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x0010,
				.disconnect_val = 0x0,
			},
		},
		.batt_info = {
			.voltage_max		= 13200, /* mV */
			.voltage_normal		= 11580, /* mV */
			.voltage_min		= 9000,  /* mV */
			.precharge_current	= 256,	 /* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 70,
		},
	},

	/* Celxpert  L19C3PG1 */
	[BATTERY_CEL] = {
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
			},
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

	/* Celxpert L20C3PG0 57W
	 * Gauge IC: TI
	 */
	[BATTERY_CEL_1] = {
		.fuel_gauge = {
			.manuf_name = "Celxpert",
			.device_name = "L20C3PG0",
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
			.precharge_current	= 200,	 /* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 70,
		},
	},

	/* SUNWODA L20D3PG1 57W
	 * Gauge IC: TI
	 */
	[BATTERY_SUNWODA] = {
		.fuel_gauge = {
			.manuf_name = "Sunwoda",
			.device_name = "L20D3PG1",
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
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 70,
		},
	},

	/* SUNWODA L20D3PG0 47W
	 * Gauge IC: TI
	 */
	[BATTERY_SUNWODA_1] = {
		.fuel_gauge = {
			.manuf_name = "Sunwoda",
			.device_name = "L20D3PG0",
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
			.precharge_current	= 205,	 /* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 70,
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_SMP;

struct chg_curr_step {
	int on;
	int off;
	int curr_ma;
};

static const struct chg_curr_step chg_curr_table[] = {
	{ .on = 0, .off = 35, .curr_ma = 2800 },
	{ .on = 36, .off = 35, .curr_ma = 1500 },
	{ .on = 39, .off = 38, .curr_ma = 1000 },
};

/* All charge current tables must have the same number of levels */
#define NUM_CHG_CURRENT_LEVELS ARRAY_SIZE(chg_curr_table)

int charger_profile_override(struct charge_state_data *curr)
{
	int rv;
	int chg_temp_c;
	int current;
	int thermal_sensor0;
	static int current_level;
	static int prev_tmp;

	/*
	 * Precharge must be executed when communication is failed on
	 * dead battery.
	 */
	if (!(curr->batt.flags & BATT_FLAG_RESPONSIVE))
		return 0;

	current = curr->requested_current;

	rv = temp_sensor_read(TEMP_SENSOR_CHARGER, &thermal_sensor0);
	chg_temp_c = K_TO_C(thermal_sensor0);

	if (rv != EC_SUCCESS)
		return 0;

	if (chipset_in_state(CHIPSET_STATE_ON)) {
		if (chg_temp_c < prev_tmp) {
			if (chg_temp_c <= chg_curr_table[current_level].off)
				current_level = current_level - 1;
		} else if (chg_temp_c > prev_tmp) {
			if (chg_temp_c >= chg_curr_table[current_level + 1].on)
				current_level = current_level + 1;
		}
		/*
		 * Prevent level always minus 0 or over table steps.
		 */
		if (current_level < 0)
			current_level = 0;
		else if (current_level >= NUM_CHG_CURRENT_LEVELS)
			current_level = NUM_CHG_CURRENT_LEVELS - 1;

		prev_tmp = chg_temp_c;
		current = chg_curr_table[current_level].curr_ma;

		curr->requested_current = MIN(curr->requested_current, current);
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
