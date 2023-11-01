/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "battery_smart.h"
#include "charge_state.h"
#include "common.h"
#include "util.h"

#define CHARGING_VOLTAGE_MV_SAFE 8400
#define CHARGING_CURRENT_MA_SAFE 1500
#define CHARGING_VOLTAGE_MV_ADJUST 8700
#define CHARGING_CURRENT_MA_ADJUST 3200

/*
 * Battery info for all sasukette battery types. Note that the fields
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
	/* SDI Battery Information */
	[BATTERY_SDI] = {
		.manuf_name = "SDI",
		.device_name = "4402D51",
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
					.cfet_mask = 0xc000,
					.cfet_off_val = 0x2000,
				},
			},
			.batt_info = {
				.voltage_max		= 8700,
				.voltage_normal		= 7700, /* mV */
				.voltage_min		= 6000, /* mV */
				.precharge_current	= 200,	/* mA */
				.start_charging_min_c	= 0,
				.start_charging_max_c	= 45,
				.charging_min_c		= 0,
				.charging_max_c		= 50,
				.discharging_min_c	= -20,
				.discharging_max_c	= 70,
			},
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_SDI;

int charger_profile_override(struct charge_state_data *curr)
{
	int current;
	int voltage;
	/* battery temp in 0.1 deg C */
	int bat_temp_c;
	const struct battery_info *batt_info;

	/*
	 * Keep track of battery temperature range:
	 *
	 *     ZONE_0  ZONE_1   ZONE_2  ZONE_3
	 * ---+------+--------+--------+------+--- Temperature (C)
	 *    0      5        12       45     50
	 */
	enum {
		TEMP_ZONE_0, /* 0 <= bat_temp_c <= 5 */
		TEMP_ZONE_1, /* 5 < bat_temp_c <= 12 */
		TEMP_ZONE_2, /* 12 < bat_temp_c <= 45 */
		TEMP_ZONE_3, /* 45 < bat_temp_c <= 50 */
		TEMP_ZONE_COUNT,
		TEMP_OUT_OF_RANGE = TEMP_ZONE_COUNT
	} temp_zone;

	/*
	 * Precharge must be executed when communication is failed on
	 * dead battery.
	 */
	if (!(curr->batt.flags & BATT_FLAG_RESPONSIVE))
		return 0;

	current = curr->requested_current;
	if (current > CHARGING_CURRENT_MA_ADJUST)
		current = CHARGING_CURRENT_MA_ADJUST;
	voltage = curr->requested_voltage;
	if (voltage > CHARGING_VOLTAGE_MV_ADJUST)
		voltage = CHARGING_VOLTAGE_MV_ADJUST;
	bat_temp_c = curr->batt.temperature - 2731;
	batt_info = battery_get_info();

	/*
	 * If the temperature reading is bad, assume the temperature
	 * is out of allowable range.
	 */
	if ((curr->batt.flags & BATT_FLAG_BAD_TEMPERATURE) ||
	    (bat_temp_c < 0) || (bat_temp_c > 500))
		temp_zone = TEMP_OUT_OF_RANGE;
	else if (bat_temp_c <= 50)
		temp_zone = TEMP_ZONE_0;
	else if (bat_temp_c <= 120)
		temp_zone = TEMP_ZONE_1;
	else if (bat_temp_c <= 450)
		temp_zone = TEMP_ZONE_2;
	else
		temp_zone = TEMP_ZONE_3;

	switch (temp_zone) {
	case TEMP_ZONE_0:
		voltage = CHARGING_VOLTAGE_MV_SAFE;
		current = CHARGING_CURRENT_MA_SAFE;
		break;

	case TEMP_ZONE_1:
		current = CHARGING_CURRENT_MA_SAFE;
		break;

	case TEMP_ZONE_2:
		break;

	case TEMP_ZONE_3:
		voltage = CHARGING_VOLTAGE_MV_SAFE;
		break;

	case TEMP_OUT_OF_RANGE:
		/* Don't charge if outside of allowable temperature range */
		current = 0;
		voltage = 0;
		curr->batt.flags &= ~BATT_FLAG_WANT_CHARGE;
		if (curr->state != ST_DISCHARGE)
			curr->state = ST_IDLE;
		break;
	}

	if (voltage > batt_info->voltage_max)
		voltage = batt_info->voltage_max;

	curr->requested_voltage = voltage;
	curr->requested_current = MIN(curr->requested_current, current);

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
