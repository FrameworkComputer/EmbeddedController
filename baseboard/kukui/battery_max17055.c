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
#include "driver/battery/max17055.h"
#include "ec_commands.h"
#include "util.h"

#define TEMP_OUT_OF_RANGE TEMP_ZONE_COUNT

#ifdef BOARD_KAKADU
#define BATT_ID 1
#else
#define BATT_ID 0
#endif

#define BATTERY_SIMPLO_CHARGE_MIN_TEMP 0
#define BATTERY_SIMPLO_CHARGE_MAX_TEMP 60

#define BATTERY_ATL_CHARGE_MIN_TEMP -20
#define BATTERY_ATL_CHARGE_MAX_TEMP 60

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

enum battery_type {
	BATTERY_SIMPLO = 0,
	BATTERY_ATL,
	BATTERY_COUNT
};

static const struct battery_info info[] = {
	[BATTERY_SIMPLO] = {
		.voltage_max		= 4400,
		.voltage_normal		= 3860,
		.voltage_min		= 3000,
		.precharge_current	= 256,
		.start_charging_min_c	= 0,
		.start_charging_max_c	= 45,
		.charging_min_c		= 0,
		.charging_max_c		= 60,
		.discharging_min_c	= -20,
		.discharging_max_c	= 60,
	},
	[BATTERY_ATL] = {
		.voltage_max		= 4370,
		.voltage_normal		= 3860,
		.voltage_min		= 3150,
		.precharge_current	= 256,
		.start_charging_min_c	= 0,
		.start_charging_max_c	= 45,
		.charging_min_c		= 0,
		.charging_max_c		= 60,
		.discharging_min_c	= -20,
		.discharging_max_c	= 60,
	},
};

static const struct max17055_batt_profile batt_profile[] = {
	[BATTERY_SIMPLO] = {
		.is_ez_config		= 1,
		.design_cap		= MAX17055_DESIGNCAP_REG(6910),
		.ichg_term		= MAX17055_ICHGTERM_REG(235),
		.v_empty_detect		= MAX17055_VEMPTY_REG(3000, 3600),
	},
	[BATTERY_ATL] = {
		.is_ez_config		= 1,
		.design_cap		= MAX17055_DESIGNCAP_REG(7270),
		.ichg_term		= MAX17055_ICHGTERM_REG(500),
		.v_empty_detect		= MAX17055_VEMPTY_REG(3000, 3600),
	},
};

static const struct max17055_alert_profile alert_profile[] = {
	[BATTERY_SIMPLO] = {
		.v_alert_mxmn = VALRT_DISABLE,
		.t_alert_mxmn = MAX17055_TALRTTH_REG(
			BATTERY_SIMPLO_CHARGE_MAX_TEMP,
			BATTERY_SIMPLO_CHARGE_MIN_TEMP),
		.s_alert_mxmn = SALRT_DISABLE,
		.i_alert_mxmn = IALRT_DISABLE,
	},
	[BATTERY_ATL] = {
		.v_alert_mxmn = VALRT_DISABLE,
		.t_alert_mxmn = MAX17055_TALRTTH_REG(
			BATTERY_ATL_CHARGE_MAX_TEMP,
			BATTERY_ATL_CHARGE_MIN_TEMP),
		.s_alert_mxmn = SALRT_DISABLE,
		.i_alert_mxmn = IALRT_DISABLE,
	},
};

const struct max17055_batt_profile *max17055_get_batt_profile(void)
{
	return &batt_profile[BATT_ID];
}

const struct max17055_alert_profile *max17055_get_alert_profile(void)
{
	return &alert_profile[BATT_ID];
}

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
#ifdef BOARD_KAKADU
	static timestamp_t deadline_48;
	static timestamp_t deadline_2;
	int cycle_count = 0, rv, val;
#endif
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
		TEMP_ZONE_3, /* t3 < bat_temp_c <= t4 */
		TEMP_ZONE_COUNT
	} temp_zone;
	static struct {
		int temp_min; /* 0.1 deg C */
		int temp_max; /* 0.1 deg C */
		int desired_current; /* mA */
		int desired_voltage; /* mV */
	} temp_zones[BATTERY_COUNT][TEMP_ZONE_COUNT] = {
		[BATTERY_SIMPLO] = {
			/* Add a empty range here to avoid TEMP_ZONE_COUNT mismatch. */
			/* TEMP_ZONE_0 */
			{BATTERY_SIMPLO_CHARGE_MIN_TEMP * 10,
				BATTERY_SIMPLO_CHARGE_MIN_TEMP * 10, 1772, 4376},
			/* TEMP_ZONE_1 */
			{BATTERY_SIMPLO_CHARGE_MIN_TEMP * 10, 150, 1772, 4376},
			/* TEMP_ZONE_2 */
			{150, 450, 4020, 4376},
			/* TEMP_ZONE_3 */
			{450, BATTERY_SIMPLO_CHARGE_MAX_TEMP * 10, 3350, 4300},
		},
		[BATTERY_ATL] = {
			/* TEMP_ZONE_0 */
			{BATTERY_ATL_CHARGE_MIN_TEMP * 10, 50, 719, 4370},
			/* TEMP_ZONE_1 */
			{50, 100, 2157, 4370},
			/* TEMP_ZONE_2 */
			{100, 450, 3595, 4370},
			/* TEMP_ZONE_3 */
			{450, BATTERY_ATL_CHARGE_MAX_TEMP * 10, 2516, 4100},
		},
	};
	BUILD_ASSERT(ARRAY_SIZE(temp_zones[BATT_ID]) == TEMP_ZONE_COUNT);
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

	if (curr->state != ST_CHARGE)
		return 0;

	switch (temp_zone) {
	case TEMP_ZONE_0:
	case TEMP_ZONE_1:
	case TEMP_ZONE_2:
	case TEMP_ZONE_3:
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

#ifdef BOARD_KAKADU
	/* Check cycle count to decrease charging voltage. */
	rv = battery_cycle_count(&val);
	if (!rv)
		cycle_count = val;
	if (cycle_count > 300 && cycle_count <= 600)
		curr->requested_voltage = 4320;
	else if (cycle_count > 600 && cycle_count <= 1000)
		curr->requested_voltage = 4300;
	else if (cycle_count > 1000)
		curr->requested_voltage = 4250;
	/* Should not keep charging voltage > 4250mV for 48hrs. */
	if ((curr->state == ST_DISCHARGE) ||
		curr->chg.voltage < 4250) {
		deadline_48.val = 0;
	/* Starting count 48hours */
	} else if (curr->state == ST_CHARGE ||
		curr->state == ST_PRECHARGE) {
		if (deadline_48.val == 0)
			deadline_48.val = get_time().val +
				CHARGER_LIMIT_TIMEOUT_HOURS * HOUR;
		/* If charging voltage keep > 4250 for 48hrs,
		set charging voltage = 4250 */
		else if (timestamp_expired(deadline_48, NULL))
			curr->requested_voltage = 4250;
	}
	/* Should not keeep battery voltage > 4100mV and
	 battery temperature > 45C for two hour */
	if (curr->state == ST_DISCHARGE ||
		curr->batt.voltage < 4100 ||
		bat_temp_c < 450) {
		deadline_2.val = 0;
	} else if (curr->state == ST_CHARGE ||
		curr->state == ST_PRECHARGE) {
		if (deadline_2.val == 0)
			deadline_2.val = get_time().val +
				CHARGER_LIMIT_TIMEOUT_HOURS_TEMP * HOUR;
		else if (timestamp_expired(deadline_2, NULL)) {
			/* Set discharge and charging voltage = 4100mV */
			if (curr->batt.voltage >= 4100) {
				curr->requested_current = 0;
				curr->requested_voltage = 4100;
			}
		}
	}
#endif

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

int get_battery_manufacturer_name(char *dest, int size)
{
	static const char * const name[] = {
		[BATTERY_SIMPLO] = "SIMPLO",
		[BATTERY_ATL] = "Celxpert",
	};
	ASSERT(dest);
	strzcpy(dest, name[BATT_ID], size);
	return EC_SUCCESS;
}

