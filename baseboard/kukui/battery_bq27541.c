/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "builtin/assert.h"
#include "charge_state.h"
#include "charger_mt6370.h"
#include "console.h"
#include "driver/tcpm/mt6370.h"
#include "ec_commands.h"
#include "hooks.h"
#include "util.h"

#define TEMP_OUT_OF_RANGE TEMP_ZONE_COUNT

#define BATT_ID 0

#define BATTERY_CPT_CHARGE_MIN_TEMP 0
#define BATTERY_CPT_CHARGE_MAX_TEMP 50

#define CHARGER_LIMIT_TIMEOUT_HOURS 48
#define CHARGER_LIMIT_TIMEOUT_HOURS_TEMP 2

#define BAT_LEVEL_PD_LIMIT 85

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)

#ifdef BATTERY_PROTECTION_POLICY
#define BATTERY_PROTECTION_TIMEOUT_HOURS 24
static int time_minute = -1;
#endif

enum battery_type { BATTERY_CPT = 0, BATTERY_COUNT };

static const struct battery_info info[] = {
	[BATTERY_CPT] = {
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
		.discharging_max_c	= 60,
	},
};

const struct battery_info *battery_get_info(void)
{
	return &info[BATT_ID];
}

int charger_profile_override(struct charge_state_data *curr)
{
	static timestamp_t deadline_48;
	static timestamp_t deadline_2;
	int cycle_count = 0, rv, val;
	unsigned char rcv = 0, rcv_cycle = 0, rcv_soh = 0;
	/* (FullCharge Capacity / Design Capacity) * 100 = SOH */
	int full_cap = 0, design_cap = 0, soh = 0;
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
		[BATTERY_CPT] = {
			/* TEMP_ZONE_0 */
			{BATTERY_CPT_CHARGE_MIN_TEMP * 10, 150, 1408, 4370},
			/* TEMP_ZONE_1 */
			{150, 430, 3520, 4370},
			/* TEMP_ZONE_2 */
			{430, 450, 2112, 4320},
			/* TEMP_ZONE_3 */
			{450, BATTERY_CPT_CHARGE_MAX_TEMP * 10, 1760, 4170},
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

	/* Check cycle count to decrease charging voltage. */
	rv = battery_cycle_count(&val);
	if (!rv)
		cycle_count = val;
	if (cycle_count > 20 && cycle_count <= 50)
		rcv_cycle = 50;
	else if (cycle_count > 50 && cycle_count <= 300)
		rcv_cycle = 65;
	else if (cycle_count > 300 && cycle_count <= 600)
		rcv_cycle = 80;
	else if (cycle_count > 600 && cycle_count <= 1000)
		rcv_cycle = 100;
	else if (cycle_count > 1000)
		rcv_cycle = 150;
	/* Check SOH to decrease charging voltage. */
	if (!battery_full_charge_capacity(&full_cap) &&
	    !battery_design_capacity(&design_cap))
		soh = ((full_cap * 100) / design_cap);
	if (soh > 70 && soh <= 75)
		rcv_soh = 50;
	else if (soh > 60 && soh <= 70)
		rcv_soh = 65;
	else if (soh > 55 && soh <= 60)
		rcv_soh = 80;
	else if (soh > 50 && soh <= 55)
		rcv_soh = 100;
	else if (soh <= 50)
		rcv_soh = 150;
	rcv = MAX(rcv_cycle, rcv_soh);
	curr->requested_voltage -= rcv;

	/* Should not keep charging voltage > 4250mV for 48hrs. */
	if ((curr->state == ST_DISCHARGE) || curr->chg.voltage < 4250) {
		deadline_48.val = 0;
		/* Starting count 48hours */
	} else if (curr->state == ST_CHARGE || curr->state == ST_PRECHARGE) {
		if (deadline_48.val == 0)
			deadline_48.val = get_time().val +
					  CHARGER_LIMIT_TIMEOUT_HOURS * HOUR;
		/* If charging voltage keep > 4250 for 48hrs,
		 * set charging voltage = 4250
		 */
		else if (timestamp_expired(deadline_48, NULL))
			curr->requested_voltage = 4250;
	}
	/* Should not keeep battery voltage > 4100mV and
	 * battery temperature > 45C for two hour
	 */
	if (curr->state == ST_DISCHARGE || curr->batt.voltage < 4100 ||
	    bat_temp_c < 450) {
		deadline_2.val = 0;
	} else if (curr->state == ST_CHARGE || curr->state == ST_PRECHARGE) {
		if (deadline_2.val == 0)
			deadline_2.val =
				get_time().val +
				CHARGER_LIMIT_TIMEOUT_HOURS_TEMP * HOUR;
		else if (timestamp_expired(deadline_2, NULL)) {
			/* Set discharge and charging voltage = 4100mV */
			if (curr->batt.voltage >= 4100) {
				curr->requested_current = 0;
				curr->requested_voltage = 4100;
			}
		}
	}

#ifdef BATTERY_PROTECTION_POLICY
	/*
	 *  In S3 and S5, the battery voltage will be limited to 4.1V after 24
	 * hours.
	 */
	if (curr->state == ST_CHARGE || curr->state == ST_PRECHARGE) {
		if (time_minute == (BATTERY_PROTECTION_TIMEOUT_HOURS * 60)) {
			curr->requested_voltage =
				MIN(4100, curr->requested_voltage);
			curr->requested_current =
				MIN(1, curr->requested_current);
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
	static const char *const name[] = {
		[BATTERY_CPT] = "AS1XXXD3Ka",
	};
	ASSERT(dest);
	strzcpy(dest, name[BATT_ID], size);
	return EC_SUCCESS;
}

#ifdef BATTERY_PROTECTION_POLICY
static void battery_protection_enable(void);
DECLARE_DEFERRED(battery_protection_enable);

static void battery_protection_enable(void)
{
	time_minute++;
	hook_call_deferred(&battery_protection_enable_data, 1 * MINUTE);
	if (time_minute == (BATTERY_PROTECTION_TIMEOUT_HOURS * 60))
		hook_call_deferred(&battery_protection_enable_data, -1);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, battery_protection_enable,
	     HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, battery_protection_enable,
	     HOOK_PRIO_DEFAULT);

static void battery_protection_disable(void)
{
	hook_call_deferred(&battery_protection_enable_data, -1);
	time_minute = -1;
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, battery_protection_disable,
	     HOOK_PRIO_DEFAULT);
#endif
