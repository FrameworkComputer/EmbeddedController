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

/*
 * Battery info for all ampton/apel battery types. Note that the fields
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
	[BATTERY_C214] = {
		.manuf_name = "AS1GUXd3KB",
		.device_name = "C214-43",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x0,
					.reg_data = { 0x10, 0x10 },
				},
				.fet = {
					.reg_addr = 0x00,
					.reg_mask = 0x2000,
					.disconnect_val = 0x2000,
				},
				.flags = FUEL_GAUGE_FLAG_MFGACC,
			},
			.batt_info = {
				.voltage_max = 13200,
				.voltage_normal = 11550,
				.voltage_min = 9000,
				.precharge_current = 256,
				.start_charging_min_c = 0,
				.start_charging_max_c = 45,
				.charging_min_c = 0,
				.discharging_min_c = 0,
				.discharging_max_c = 60,
			},
		},
	},
	[BATTERY_C204EE] = {
		.manuf_name = "AS1GVCD3KB",
		.device_name = "C204-35",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x0,
					.reg_data = { 0x10, 0x10 },
				},
				.fet = {
					.reg_addr = 0x00,
					.reg_mask = 0x2000,
					.disconnect_val = 0x2000,
				},
				.flags = FUEL_GAUGE_FLAG_MFGACC,
			},
			.batt_info = {
				.voltage_max = 13200,
				.voltage_normal = 11550,
				.voltage_min = 9000,
				.precharge_current = 256,
				.start_charging_min_c = 0,
				.start_charging_max_c = 45,
				.charging_min_c = 0,
				.discharging_min_c = 0,
				.discharging_max_c = 60,
			},
		},
	},
	[BATTERY_C424] = {
		.manuf_name = "AS2GVID3jB",
		.device_name = "C424-35",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x0,
					.reg_data = { 0x10, 0x10 },
				},
				.fet = {
					.reg_addr = 0x00,
					.reg_mask = 0x2000,
					.disconnect_val = 0x2000,
				},
				.flags = FUEL_GAUGE_FLAG_MFGACC,
			},
			.batt_info = {
				.voltage_max = 13200,
				.voltage_normal = 11550,
				.voltage_min = 9000,
				.precharge_current = 256,
				.start_charging_min_c = 0,
				.start_charging_max_c = 45,
				.charging_min_c = 0,
				.discharging_min_c = 0,
				.discharging_max_c = 60,
			},
		},
	},
	[BATTERY_C204_SECOND] = {
		.manuf_name = "AS3FXXd3KB",
		.device_name = "C214-43",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x0,
					.reg_data = { 0x10, 0x10 },
				},
				.fet = {
					.reg_addr = 0x99,
					.reg_mask = 0x000C,
					.disconnect_val = 0x000C,
					.cfet_mask = 0x0004,
					.cfet_off_val = 0x0004
				},
			},
			.batt_info = {
				.voltage_max = 13200,
				.voltage_normal = 11550,
				.voltage_min = 9000,
				.precharge_current = 256,
				.start_charging_min_c = 0,
				.start_charging_max_c = 45,
				.charging_min_c = 0,
				.discharging_min_c = -20,
				.discharging_max_c = 60,
			},
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_C214;
