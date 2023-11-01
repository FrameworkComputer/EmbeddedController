/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery_fuel_gauge.h"
#include "charge_state.h"
#include "common.h"
#include "system.h"
#include "util.h"

/*
 * Battery info for all Nightfury battery types. Note that the fields
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

/* charging current is limited to 0.45C */
#define CHARGING_CURRENT_45C 2804
#define CHARGING_CURRENT_NORMAL 3640
#define CHARGING_VOLTAGE_NORMAL 8650

const struct batt_conf_embed board_battery_info[] = {
	/* Dyna Battery Information */
	[BATTERY_DYNA] = {
		.manuf_name = "Dyna",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x0,
					.reg_data = { 0x10, 0x10 },
				},
				.fet = {
					.reg_addr = 0x0,
					.reg_mask = 0x2000,
					.disconnect_val = 0x2000,
				},
			},
			.batt_info = {
				.voltage_max		= 8650,
				.voltage_normal		= 7600, /* mV */
				.voltage_min		= 6000, /* mV */
				.precharge_current	= 150,	/* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 45,
				.charging_min_c		= 0,
				.charging_max_c		= 60,
				.discharging_min_c	= -20,
				.discharging_max_c	= 60,
			},
		},
	},
	[BATTERY_SDI] = {
		.manuf_name = "SDI",
		.device_name = "4404D57",
		.config = {
			.fuel_gauge = {
				.ship_mode = {
					.reg_addr = 0x00,
					.reg_data = { 0x0010, 0x0010 },
				},
				.fet = {
					.reg_addr = 0x00,
					.reg_mask = 0xc000,
					.disconnect_val = 0x8000,
				},
			},
			.batt_info = {
				.voltage_max            = 8650,
				.voltage_normal         = 7700, /* mV */
				.voltage_min            = 6000, /* mV */
				.precharge_current      = 200,  /* mA */
				.start_charging_min_c   = 0,
				.start_charging_max_c   = 45,
				.charging_min_c         = 0,
				.charging_max_c         = 55,
				.discharging_min_c      = -20,
				.discharging_max_c      = 70,
			},
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_SDI;

enum battery_present variant_battery_present(void)
{
	/*
	 * For board version 1, there is a known issue with battery present
	 * signal. So, always return BP_YES indicating battery is
	 * present. battery_status() later should fail to talk to the battery in
	 * case the battery is not really present.
	 */
	return BP_YES;
}

int charger_profile_override(struct charge_state_data *curr)
{
	if (curr->requested_current > CHARGING_CURRENT_NORMAL)
		curr->requested_current = CHARGING_CURRENT_NORMAL;
	if (curr->requested_voltage > CHARGING_VOLTAGE_NORMAL)
		curr->requested_voltage = CHARGING_VOLTAGE_NORMAL;

	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		return 0;

	if (curr->requested_current > CHARGING_CURRENT_45C)
		curr->requested_current = CHARGING_CURRENT_45C;

	return 0;
}

/* Customs options controllable by host command. */
#define PARAM_FASTCHARGE (CS_PARAM_CUSTOM_PROFILE_MIN + 0)

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
