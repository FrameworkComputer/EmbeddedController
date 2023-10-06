/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery_fuel_gauge.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "hooks.h"
#include "usb_pd.h"

enum battery_imbalance_mv {
	BATTERY_IMBALANCE_MV_BQ4050 = BIT(0),
};

/*
 * Battery info for all Liara battery types. Note that the fields
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
	/*
	 * Panasonic AP15O5L battery information from the Grunt reference
	 * design.
	 */
	[BATTERY_PANASONIC] = {
		.fuel_gauge = {
			.manuf_name = "PANASONIC",
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
			.voltage_min		= 9000,  /* mV */
			.precharge_current	= 256,   /* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= 0,
			.discharging_max_c	= 60,
		},
	},
	/*
	 * Sunwoda 2018 Battery Information for Liara.
	 * Gauge IC: TI BQ40Z697A
	 */
	[BATTERY_SUNWODA] = {
		.fuel_gauge = {
			.manuf_name = "Sunwoda 2018",
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
			.board_flags = BATTERY_IMBALANCE_MV_BQ4050,
		},
		.batt_info = {
			.voltage_max		= 13200,
			.voltage_normal		= 11520, /* mV */
			.voltage_min		= 9000,  /* mV */
			.precharge_current	= 200,   /* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 70,
		},
	},
	/*
	 * Simplo 2018 Battery Information for Liara
	 * Gauge IC: TI BQ40Z695A
	 */
	[BATTERY_SIMPLO] = {
		.fuel_gauge = {
			.manuf_name = "SMP2018",
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
			.board_flags = BATTERY_IMBALANCE_MV_BQ4050,
		},
		.batt_info = {
			.voltage_max		= 13200,
			.voltage_normal		= 11520, /* mV */
			.voltage_min		= 9000,  /* mV */
			.precharge_current	= 247,   /* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 70,
		},
	},
	/*
	 * LGC 2018 Battery Information for Liara
	 * Gauge IC: Renesas RAJ240047A20DNP
	 */
	[BATTERY_LGC] = {
		.fuel_gauge = {
			.manuf_name = "LGC2018",
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
			.voltage_max		= 13200,
			.voltage_normal		= 11520, /* mV */
			.voltage_min		= 9000,  /* mV */
			.precharge_current	= 256,   /* mA */
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

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_PANASONIC;

__override int board_battery_imbalance_mv(const struct board_batt_params *info)
{
	if (info->fuel_gauge.board_flags & BATTERY_IMBALANCE_MV_BQ4050)
		return battery_bq4050_imbalance_mv();
	else
		return 0;
}
