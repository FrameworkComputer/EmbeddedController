/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "battery_smart.h"
#include "charge_state.h"
#include "chipset.h"
#include "console.h"
#include "driver/battery/max17055.h"
#include "driver/charger/rt946x.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "system.h"
#include "util.h"

/*
 * AE-Tech battery pack has two charging phases when operating
 * between 10 and 20C
 */
#define CHARGE_PHASE_CHANGE_TRIP_VOLTAGE_MV 4200
#define CHARGE_PHASE_CHANGE_HYSTERESIS_MV 50
#define CHARGE_PHASE_CHANGED_CURRENT_MA 1800

#define TEMP_OUT_OF_RANGE TEMP_ZONE_COUNT

static uint8_t batt_id = 0xff;

/* Do not change the enum values. We directly use strap gpio level to index. */
enum battery_type {
	BATTERY_SIMPLO = 0,
	BATTERY_AETECH,
	BATTERY_COUNT
};

static const struct battery_info info[] = {
	[BATTERY_SIMPLO] = {
		.voltage_max		= 4400,
		.voltage_normal		= 3840,
		.voltage_min		= 3000,
		.precharge_current	= 256,
		.start_charging_min_c	= 0,
		.start_charging_max_c	= 45,
		.charging_min_c		= 0,
		.charging_max_c		= 60,
		.discharging_min_c	= -20,
		.discharging_max_c	= 60,
	},
	[BATTERY_AETECH] = {
		.voltage_max		= 4350,
		.voltage_normal		= 3800,
		.voltage_min		= 3000,
		.precharge_current	= 700,
		.start_charging_min_c	= 0,
		.start_charging_max_c	= 45,
		.charging_min_c		= 0,
		.charging_max_c		= 45,
		.discharging_min_c	= -20,
		.discharging_max_c	= 55,
	}
};

static const struct max17055_batt_profile batt_profile[] = {
	[BATTERY_SIMPLO] = {
		.is_ez_config		= 0,
		.design_cap		= 0x221e, /* 8734mAh */
		.ichg_term		= 0x589, /* 443 mA */
		/* Empty voltage = 3000mV, Recovery voltage = 3600mV */
		.v_empty_detect		= 0x965a,
		.learn_cfg		= 0x4406,
		.dpacc			= 0x0c7a,
		.rcomp0			= 0x0062,
		.tempco			= 0x1327,
		.qr_table00		= 0x1680,
		.qr_table10		= 0x0900,
		.qr_table20		= 0x0280,
		.qr_table30		= 0x0280,
	},
	[BATTERY_AETECH] = {
		.is_ez_config		= 0,
		.design_cap		= 0x232f, /* 9007mAh */
		.ichg_term		= 0x0240, /* 180mA */
		/* Empty voltage = 2700mV, Recovery voltage = 3280mV */
		.v_empty_detect		= 0x8752,
		.learn_cfg		= 0x4476,
		.dpacc			= 0x0c7b,
		.rcomp0			= 0x0077,
		.tempco			= 0x1d3f,
		.qr_table00		= 0x1200,
		.qr_table10		= 0x0900,
		.qr_table20		= 0x0480,
		.qr_table30		= 0x0480,
	},
};

const struct battery_info *battery_get_info(void)
{
	if (batt_id >= BATTERY_COUNT)
		batt_id = gpio_get_level(GPIO_BATT_ID);

	return &info[batt_id];
}

const struct max17055_batt_profile *max17055_get_batt_profile(void)
{
	if (batt_id >= BATTERY_COUNT)
		batt_id = gpio_get_level(GPIO_BATT_ID);

	return &batt_profile[batt_id];
}

int board_cut_off_battery(void)
{
	return rt946x_cutoff_battery();
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
		[BATTERY_SIMPLO] = {
			{0, 150, 1772, 4376}, /* TEMP_ZONE_0 */
			{150, 450, 4000, 4376}, /* TEMP_ZONE_1 */
			{450, 600, 4000, 4100}, /* TEMP_ZONE_2 */
		},
		[BATTERY_AETECH] = {
			{0, 100, 900, 4200}, /* TEMP_ZONE_0 */
			{100, 200, 2700, 4350}, /* TEMP_ZONE_1 */
			/*
			 * TODO(b:70287349): Limit the charging current to
			 * 2A unless AE-Tech fix their battery pack.
			 */
			{200, 450, 2000, 4350}, /* TEMP_ZONE_2 */
		}
	};
	BUILD_ASSERT(ARRAY_SIZE(temp_zones[0]) == TEMP_ZONE_COUNT);
	BUILD_ASSERT(ARRAY_SIZE(temp_zones) == BATTERY_COUNT);

	static int charge_phase = 1;
	static uint8_t quirk_batt_update;

	/*
	 * This is a quirk for old Simplo battery to clamp
	 * charging current to 3A.
	 */
	if ((board_get_version() <= 4) && !quirk_batt_update) {
		temp_zones[BATTERY_SIMPLO][TEMP_ZONE_1].desired_current = 3000;
		temp_zones[BATTERY_SIMPLO][TEMP_ZONE_2].desired_current = 3000;
		quirk_batt_update = 1;
	}

	if (batt_id >= BATTERY_COUNT)
		batt_id = gpio_get_level(GPIO_BATT_ID);

	if ((curr->batt.flags & BATT_FLAG_BAD_TEMPERATURE) ||
	    (bat_temp_c < temp_zones[batt_id][0].temp_min) ||
	    (bat_temp_c >= temp_zones[batt_id][TEMP_ZONE_COUNT - 1].temp_max))
		temp_zone = TEMP_OUT_OF_RANGE;
	else {
		for (temp_zone = 0; temp_zone < TEMP_ZONE_COUNT; temp_zone++) {
			if (bat_temp_c <
				temp_zones[batt_id][temp_zone].temp_max)
				break;
		}
	}

	if (curr->state != ST_CHARGE) {
		charge_phase = 1;
		return 0;
	}

	switch (temp_zone) {
	case TEMP_ZONE_0:
	case TEMP_ZONE_2:
		curr->requested_current =
			temp_zones[batt_id][temp_zone].desired_current;
		curr->requested_voltage =
			temp_zones[batt_id][temp_zone].desired_voltage;
		break;
	case TEMP_ZONE_1:
		/* No phase change for Simplo battery pack */
		if (batt_id == BATTERY_SIMPLO)
			charge_phase = 0;
		/*
		 * If AE-Tech battery pack is used and the voltage reading
		 * is bad, let's be conservative and assume change_phase == 1.
		 */
		else if (curr->batt.flags & BATT_FLAG_BAD_VOLTAGE)
			charge_phase = 1;
		else {
			if (curr->batt.voltage <
			    (CHARGE_PHASE_CHANGE_TRIP_VOLTAGE_MV -
			     CHARGE_PHASE_CHANGE_HYSTERESIS_MV))
				charge_phase = 0;
			else if (curr->batt.voltage >
				 CHARGE_PHASE_CHANGE_TRIP_VOLTAGE_MV)
				charge_phase = 1;
		}

		curr->requested_voltage =
			temp_zones[batt_id][temp_zone].desired_voltage;

		curr->requested_current = (charge_phase) ?
			CHARGE_PHASE_CHANGED_CURRENT_MA :
			temp_zones[batt_id][temp_zone].desired_current;
		break;
	case TEMP_OUT_OF_RANGE:
		curr->requested_current = curr->requested_voltage = 0;
		curr->batt.flags &= ~BATT_FLAG_WANT_CHARGE;
		curr->state = ST_IDLE;
		break;
	}

	/*
	 * When the charger says it's done charging, even if fuel gauge says
	 * SOC < BATTERY_LEVEL_NEAR_FULL, we'll overwrite SOC with
	 * BATTERY_LEVEL_NEAR_FULL. So we can ensure both Chrome OS UI
	 * and battery LED indicate full charge.
	 */
	if (rt946x_is_charge_done()) {
		curr->batt.state_of_charge = MAX(BATTERY_LEVEL_NEAR_FULL,
						 curr->batt.state_of_charge);
		/*
		 * This is a workaround for b:78792296. When AP is off and
		 * charge termination is detected, we disable idle mode.
		 */
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
			disable_idle();
		else
			enable_idle();
	}

	return 0;
}

static void board_enable_idle(void)
{
	enable_idle();
}
DECLARE_HOOK(HOOK_AC_CHANGE, board_enable_idle, HOOK_PRIO_DEFAULT);

static void board_charge_termination(void)
{
	static uint8_t te;
	/* Enable charge termination when we are sure battery is present. */
	if (!te && battery_is_present() == BP_YES) {
		if (!rt946x_enable_charge_termination(1))
			te = 1;
	}
}
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE,
	     board_charge_termination,
	     HOOK_PRIO_DEFAULT);

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
