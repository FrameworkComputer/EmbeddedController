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
#include "hooks.h"
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
	/* CM1500 50Wh */
	[BATTERY_CM1500] = {
		.fuel_gauge = {
			.manuf_name = "AS3GXXD3KB",
			.device_name = "C140243",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = 0x99,
				.reg_mask = 0x000c,
				.disconnect_val = 0x000c,
			},
		},
		.batt_info = {
			.voltage_max		= 13200, /* mV */
			.voltage_normal		= 11880, /* mV */
			.voltage_min		= 9000,  /* mV */
			.precharge_current	= 256,	 /* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 45,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 60,
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_CM1500;

static uint16_t current_table[] = {
	2200,
	1800,
	1700,
	1600,
};
#define NUM_CURRENT_LEVELS ARRAY_SIZE(current_table)

#define TEMP_THRESHOLD 54
static int current_level;

/* Called by hook task every hook second (1 sec) */
static void current_update(void)
{
	int t, temp;
	int rv;
	static int Uptime;
	static int Dntime;

	rv = temp_sensor_read(TEMP_SENSOR_CHARGER, &t);
	if (rv != EC_SUCCESS)
		return;

	temp = K_TO_C(t);

	if (temp > TEMP_THRESHOLD) {
		Dntime = 0;
		if (Uptime < 5)
			Uptime++;
		else {
			Uptime = 0;
			current_level++;
		}
	} else if (current_level != 0 && temp < TEMP_THRESHOLD) {
		Uptime = 0;
		if (Dntime < 5)
			Dntime++;
		else {
			Dntime = 0;
			current_level--;
		}
	} else {
		Uptime = 0;
		Dntime = 0;
	}

	if (current_level < 0)
		current_level = 0;
	else if (current_level > NUM_CURRENT_LEVELS)
		current_level = NUM_CURRENT_LEVELS;
}
DECLARE_HOOK(HOOK_SECOND, current_update, HOOK_PRIO_DEFAULT);

int charger_profile_override(struct charge_state_data *curr)
{
	/*
	 * Precharge must be executed when communication is failed on
	 * dead battery.
	 */
	if (!(curr->batt.flags & BATT_FLAG_RESPONSIVE))
		return 0;

	if (current_level != 0) {
		if (curr->requested_current > current_table[current_level - 1])
			curr->requested_current =
				current_table[current_level - 1];
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
