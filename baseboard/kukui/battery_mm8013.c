/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "charge_state.h"
#include "charger_mt6370.h"
#include "console.h"
#include "driver/tcpm/mt6370.h"
#include "ec_commands.h"
#include "util.h"

#define TEMP_OUT_OF_RANGE TEMP_ZONE_COUNT

#define BATT_ID 0

#define BATTERY_SCUD_CHARGE_MIN_TEMP 0
#define BATTERY_SCUD_CHARGE_MAX_TEMP 50

#define BAT_LEVEL_PD_LIMIT 85

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

enum battery_type {
	BATTERY_SCUD = 0,
	BATTERY_COUNT
};

static const struct battery_info info[] = {
	[BATTERY_SCUD] = {
		.voltage_max		= 4400,
		.voltage_normal		= 3850,
		.voltage_min		= 3000,
		.precharge_voltage	= 3400,
		.precharge_current	= 256,
		.start_charging_min_c	= 0,
		.start_charging_max_c	= 45,
		.charging_min_c		= 0,
		.charging_max_c		= 50,
		.discharging_min_c	= -20,
		.discharging_max_c	= 59,
	},
};

const struct battery_info *battery_get_info(void)
{
	return &info[BATT_ID];
}

enum battery_disconnect_state battery_get_disconnect_state(void)
{
	if (battery_is_present() == BP_YES)
		return BATTERY_NOT_DISCONNECTED;
	return BATTERY_DISCONNECTED;
}

int charger_profile_override(struct charge_state_data *curr)
{
	/* battery temp in 0.1 deg C */
	int bat_temp_c = curr->batt.temperature - 2731;
	/*
	 * Keep track of battery temperature range:
	 *
	 *        ZONE_0   ZONE_1     ZONE_2
	 * -----+--------+--------+------------+----- Temperature (C)
	 *      t0       t1       t2           t3
	 */
	enum {
		TEMP_ZONE_0, /* t0 < bat_temp_c <= t1 */
		TEMP_ZONE_1, /* t1 < bat_temp_c <= t2 */
		TEMP_ZONE_2, /* t2 < bat_temp_c <= t3 */
		TEMP_ZONE_COUNT
	} temp_zone;

	static struct {
		int temp_min; /* 0.1 deg C */
		int temp_max; /* 0.1 deg C */
		int desired_current; /* mA */
		int desired_voltage; /* mV */
	} temp_zones[BATTERY_COUNT][TEMP_ZONE_COUNT] = {
		[BATTERY_SCUD] = {
		/* TEMP_ZONE_0 */
		{BATTERY_SCUD_CHARGE_MIN_TEMP * 10, 150, 1400, 4400},
		/* TEMP_ZONE_1 */
		{150, 450, 3500, 4400},
		/* TEMP_ZONE_2 */
		{450, BATTERY_SCUD_CHARGE_MAX_TEMP * 10, 3500, 4200},
		},
	};
	BUILD_ASSERT(ARRAY_SIZE(temp_zones[0]) == TEMP_ZONE_COUNT);
	BUILD_ASSERT(ARRAY_SIZE(temp_zones) == BATTERY_COUNT);

	if ((curr->batt.flags & BATT_FLAG_BAD_TEMPERATURE) ||
	    (bat_temp_c < temp_zones[BATT_ID][0].temp_min) ||
	    (bat_temp_c >= temp_zones[BATT_ID][TEMP_ZONE_COUNT - 1].temp_max))
		temp_zone = TEMP_OUT_OF_RANGE;
	else {
		for (temp_zone = 0; temp_zone < TEMP_ZONE_COUNT; temp_zone++) {
			if (bat_temp_c <
				temp_zones[BATT_ID][temp_zone].temp_max)
				break;
		}
	}

	switch (temp_zone) {
	case TEMP_ZONE_0:
	case TEMP_ZONE_1:
	case TEMP_ZONE_2:
		curr->requested_current =
			temp_zones[BATT_ID][temp_zone].desired_current;
		curr->requested_voltage =
			temp_zones[BATT_ID][temp_zone].desired_voltage;
		break;
	case TEMP_OUT_OF_RANGE:
		curr->requested_current = curr->requested_voltage = 0;
		curr->batt.flags &= ~BATT_FLAG_WANT_CHARGE;
		curr->state = ST_IDLE;
		break;
	}

#ifdef VARIANT_KUKUI_CHARGER_MT6370
	mt6370_charger_profile_override(curr);
#endif /* CONFIG_CHARGER_MT6370 */

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
