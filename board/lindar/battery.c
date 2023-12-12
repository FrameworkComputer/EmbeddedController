/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "battery_smart.h"
#include "common.h"
#include "util.h"

/*
 * Battery info for all Volteer battery types. Note that the fields
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
	[BATTERY_SMP] = {
		.manuf_name = "SMP",
		.device_name = "L19M4PG2",
		.config = {
			.fuel_gauge = {
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
				.voltage_max		= 8800, /* mV */
				.voltage_normal		= 7680, /* mV */
				.voltage_min		= 6000, /* mV */
				.precharge_current	= 332,	/* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 50,
				.charging_min_c		= 0,
				.charging_max_c		= 60,
				.discharging_min_c	= -20,
				.discharging_max_c	= 60,
			},
		},
	},
	[BATTERY_LGC] = {
		.manuf_name = "LGC",
		.device_name = "L19L4PG2",
		.config = {
			.fuel_gauge = {
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
				.voltage_max		= 8800, /* mV */
				.voltage_normal		= 7700, /* mV */
				/*
				 * voltage min value and precharge current value
				 * are specified by LGC directly and not shown
				 * in the SPEC.
				 */
				.voltage_min		= 6000, /* mV */
				.precharge_current	= 256,	/* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 50,
				.charging_min_c		= 0,
				.charging_max_c		= 60,
				.discharging_min_c	= -20,
				.discharging_max_c	= 73,
			},
		},
	},
	[BATTERY_SUNWODA] = {
		.manuf_name = "SUNWODA",
		.device_name = "L19D4PG2",
		.config = {
			.fuel_gauge = {
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
				.voltage_max		= 8800, /* mV */
				.voltage_normal		= 7680, /* mV */
				.voltage_min		= 6000, /* mV */
				.precharge_current	= 333,	/* mA */
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

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_SMP;

__override bool board_battery_is_initialized(void)
{
	bool batt_initialization_state;
	int batt_status;

	batt_initialization_state =
		(battery_status(&batt_status) ?
			 false :
			 !!(batt_status & STATUS_INITIALIZED));
	return batt_initialization_state;
}
