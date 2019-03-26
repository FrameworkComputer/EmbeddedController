/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "battery_smart.h"
#include "charge_state.h"
#include "console.h"
#include "driver/battery/max17055.h"
#include "driver/charger/rt946x.h"
#include "driver/tcpm/mt6370.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "usb_pd.h"
#include "util.h"
#include "board.h"
#include "adc.h"
#include "adc_chip.h"
#include "math_util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#define BAT_LEVEL_PD_LIMIT 85

#define BATTERY_ATL_CHARGE_MIN_TEMP 0
#define BATTERY_ATL_CHARGE_MAX_TEMP 60

static const struct battery_info info[] = {
	[BATTERY_C18_ATL] = {
		.voltage_max		= 4400,
		.voltage_normal		= 3850,
		.voltage_min		= 3000,
		.precharge_current	= 294,
		.start_charging_min_c	= 0,
		.start_charging_max_c	= 45,
		.charging_min_c		= 0,
		.charging_max_c		= 60,
		.discharging_min_c	= -20,
		.discharging_max_c	= 60,
	},
	[BATTERY_C19_ATL] = {
		.voltage_max		= 4400,
		.voltage_normal		= 3850,
		.voltage_min		= 3000,
		.precharge_current	= 327,
		.start_charging_min_c	= 0,
		.start_charging_max_c	= 45,
		.charging_min_c		= 0,
		.charging_max_c		= 60,
		.discharging_min_c	= -20,
		.discharging_max_c	= 60,
	},
};
BUILD_ASSERT(ARRAY_SIZE(info) == BATTERY_COUNT);

static const struct max17055_batt_profile batt_profile[] = {
	[BATTERY_C18_ATL] = {
		.is_ez_config		= 0,
		.design_cap		= 0x2e78, /* 5948mAh */
		.ichg_term		= 0x02ec, /* 117 mA */
		/* Empty voltage = 3400mV, Recovery voltage = 4000mV */
		.v_empty_detect		= 0xaa64,
		.learn_cfg		= 0x4402,
		.dpacc			= 0x0c7d,
		.rcomp0			= 0x0011,
		.tempco			= 0x0209,
		.qr_table00		= 0x5a00,
		.qr_table10		= 0x2980,
		.qr_table20		= 0x1100,
		.qr_table30		= 0x1000,
	},
	[BATTERY_C19_ATL] = {
		.is_ez_config		= 0,
		.design_cap		= 0x3407, /* 6659mAh */
		.ichg_term		= 0x0340, /* 130mA */
		/* Empty voltage = 3400mV, Recovery voltage = 4000mV */
		.v_empty_detect		= 0xaa64,
		.learn_cfg		= 0x4402,
		.dpacc			= 0x0c7e,
		.rcomp0			= 0x000f,
		.tempco			= 0x000b,
		.qr_table00		= 0x5800,
		.qr_table10		= 0x2680,
		.qr_table20		= 0x0d00,
		.qr_table30		= 0x0b00,
	},
};
BUILD_ASSERT(ARRAY_SIZE(batt_profile) == BATTERY_COUNT);

static const struct max17055_alert_profile alert_profile[] = {
	[BATTERY_C18_ATL] = {
		.v_alert_mxmn = VALRT_DISABLE,
		.t_alert_mxmn = MAX17055_TALRTTH_REG(
			BATTERY_ATL_CHARGE_MAX_TEMP,
			BATTERY_ATL_CHARGE_MIN_TEMP),
		.s_alert_mxmn = SALRT_DISABLE,
		.i_alert_mxmn = IALRT_DISABLE,
	},
	[BATTERY_C19_ATL] = {
		.v_alert_mxmn = VALRT_DISABLE,
		.t_alert_mxmn = MAX17055_TALRTTH_REG(
			BATTERY_ATL_CHARGE_MAX_TEMP,
			BATTERY_ATL_CHARGE_MIN_TEMP),
		.s_alert_mxmn = SALRT_DISABLE,
		.i_alert_mxmn = IALRT_DISABLE,
	},
};
BUILD_ASSERT(ARRAY_SIZE(alert_profile) == BATTERY_COUNT);

enum {
	TEMP_ZONE_0, /* t0 <= bat_temp_c < t1  */
	TEMP_ZONE_1, /* t1 <= bat_temp_c < t2 */
	TEMP_ZONE_2, /* t2 <= bat_temp_c < t3 */
	TEMP_ZONE_3, /* t3 <= bat_temp_c < t4 */
	TEMP_ZONE_COUNT,
	TEMP_OUT_OF_RANGE = TEMP_ZONE_COUNT,
} temp_zone;

static const struct {
	int temp_min; /* 0.1 deg C */
	int temp_max; /* 0.1 deg C */
	int desired_current; /* mA */
	int desired_voltage; /* mV */
} temp_zones[BATTERY_COUNT][TEMP_ZONE_COUNT] = {
	[BATTERY_C18_ATL] = {
		/* TEMP_ZONE_0 */
		{BATTERY_ATL_CHARGE_MIN_TEMP * 10, 10, 1170, 4400},
		/* TEMP_ZONE_1 */
		{100, 200, 1755, 4400},
		/* TEMP_ZONE_2 */
		{200, 450, 2925, 4400},
		/* TEMP_ZONE_3 */
		{450, BATTERY_ATL_CHARGE_MAX_TEMP * 10, 2925, 4100},
	},
	[BATTERY_C19_ATL] = {
		/* TEMP_ZONE_0 */
		{BATTERY_ATL_CHARGE_MIN_TEMP * 10, 10, 1300, 4400},
		/* TEMP_ZONE_1 */
		{100, 200, 1950, 4400},
		/* TEMP_ZONE_2 */
		{200, 450, 3250, 4400},
		/* TEMP_ZONE_3 */
		{450, BATTERY_ATL_CHARGE_MAX_TEMP * 10, 3250, 4100},
	},
};

static struct {
	enum battery_type type;
	int expect_mv;
} const batteries[] = {
	{ BATTERY_C18_ATL,       900 },  /* 100K  */
	{ BATTERY_C19_ATL,       576 },  /* 47K   */
};
BUILD_ASSERT(ARRAY_SIZE(batteries) < BATTERY_COUNT);

#define MARGIN_MV 56 /* Simply assume 1800/16/2 */

static enum battery_type batt_type = BATTERY_UNKNOWN;

static void board_get_battery_type(void)
{
	int mv;
	int i;

	mv = adc_read_channel(ADC_BATT_ID);
	if (mv == ADC_READ_ERROR)
		mv = adc_read_channel(ADC_BATT_ID);

	for (i = 0; i < ARRAY_SIZE(batteries); i++) {
		if (ABS(mv - batteries[i].expect_mv) < MARGIN_MV) {
			batt_type = batteries[i].type;
			break;
		}
	}

	CPRINTS("Battery Type: %d", batt_type);
}
DECLARE_HOOK(HOOK_INIT, board_get_battery_type, HOOK_PRIO_FIRST);

const struct battery_info *battery_get_info(void)
{
	return &info[batt_type];
}

const struct max17055_batt_profile *max17055_get_batt_profile(void)
{
	return &batt_profile[batt_type];
}

const struct max17055_alert_profile *max17055_get_alert_profile(void)
{
	return &alert_profile[batt_type];
}

int board_cut_off_battery(void)
{
	/* The cut-off procedure is recommended by Richtek. b/116682788 */
	rt946x_por_reset();
	mt6370_vconn_discharge(0);
	rt946x_cutoff_battery();

	return EC_SUCCESS;
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

	if (curr->state != ST_CHARGE)
		return 0;

	if ((curr->batt.flags & BATT_FLAG_BAD_TEMPERATURE) ||
	    (bat_temp_c < temp_zones[batt_type][0].temp_min) ||
	    (bat_temp_c >= temp_zones[batt_type][TEMP_ZONE_COUNT - 1].temp_max))
		temp_zone = TEMP_OUT_OF_RANGE;
	else {
		for (temp_zone = TEMP_ZONE_0; temp_zone < TEMP_ZONE_COUNT;
		     temp_zone++) {
			if (bat_temp_c <
				temp_zones[batt_type][temp_zone].temp_max)
				break;
		}
	}

	if (temp_zone == TEMP_OUT_OF_RANGE) {
		curr->requested_current = curr->requested_voltage = 0;
		curr->batt.flags &= ~BATT_FLAG_WANT_CHARGE;
		curr->state = ST_IDLE;
	} else {
		curr->requested_current =
			temp_zones[batt_type][temp_zone].desired_current;
		curr->requested_voltage =
			temp_zones[batt_type][temp_zone].desired_voltage;
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
	}

	return 0;
}

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

static void pd_limit_5v(uint8_t en)
{
	int wanted_pd_voltage;

	wanted_pd_voltage = en ? 5500 : PD_MAX_VOLTAGE_MV;

	if (pd_get_max_voltage() != wanted_pd_voltage)
		pd_set_external_voltage_limit(0, wanted_pd_voltage);
}

/* When battery level > BAT_LEVEL_PD_LIMIT, we limit PD voltage to 5V. */
static void board_pd_voltage(void)
{
	pd_limit_5v(charge_get_percent() > BAT_LEVEL_PD_LIMIT);
}
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, board_pd_voltage, HOOK_PRIO_DEFAULT);

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
