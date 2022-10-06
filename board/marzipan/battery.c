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
 * Battery info for all marzipan battery types. Note that the fields
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
	/* COSMX DM02032XL 333-AC-13-A */
	[BATTERY_COSMX] = {
		.fuel_gauge = {
			.manuf_name = "333-AC-13-A",
			.device_name = "DM02032XL",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.mfgacc_support = 1,
				.reg_addr = 0x0,
				.reg_mask = 0x0006,
				.disconnect_val = 0x0,
			}
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
		},
	},
	/* Simplo DM02032XL 333-AC-13-A */
	[BATTERY_SIMPLO] = {
		.fuel_gauge = {
			.manuf_name = "333-1D-13-A",
			.device_name = "DM02032XL",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.mfgacc_support = 1,
				.reg_addr = 0x0,
				.reg_mask = 0x0006,
				.disconnect_val = 0x0,
			}
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
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_COSMX;
