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
#include "i2c.h"
#include "usb_pd.h"
#include "util.h"
#include "board.h"
#include "adc.h"
#include "adc_chip.h"
#include "math_util.h"
#include "p9221.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#define BAT_LEVEL_PD_LIMIT 85

#define BATTERY_ATL_CHARGE_MIN_TEMP 0
#define BATTERY_ATL_CHARGE_MAX_TEMP 60

#define BATTERY_SUNWODA_CHARGE_MIN_TEMP 0
#define BATTERY_SUNWODA_CHARGE_MAX_TEMP 60

static const uint16_t full_model_ocv_table[][MAX17055_OCV_TABLE_SIZE] = {
	[BATTERY_C18_ATL] = {
		0x8fc0, 0xb6c0, 0xb910, 0xbb30, 0xbcb0, 0xbdd0, 0xbef0, 0xc050,
		0xc1a0, 0xc460, 0xc750, 0xca40, 0xcd10, 0xd070, 0xd560, 0xda20,
		0x0060, 0x0f20, 0x0f40, 0x16c0, 0x17f0, 0x15c0, 0x1050, 0x10e0,
		0x09f0, 0x0850, 0x0730, 0x07a0, 0x0730, 0x0700, 0x0710, 0x0710,
		0x0800, 0x0800, 0x0800, 0x0800, 0x0800, 0x0800, 0x0800, 0x0800,
		0x0800, 0x0800, 0x0800, 0x0800, 0x0800, 0x0800, 0x0800, 0x0800,
	},
	[BATTERY_C19_ATL] = {
		0xa260, 0xb5d0, 0xb840, 0xb940, 0xbbb0, 0xbcb0, 0xbdb0, 0xbf80,
		0xc0a0, 0xc1e0, 0xc520, 0xc840, 0xcdb0, 0xd150, 0xd590, 0xd9e0,
		0x0030, 0x0cd0, 0x1100, 0x0f30, 0x19e0, 0x19f0, 0x14f0, 0x1160,
		0x0dc0, 0x0980, 0x0850, 0x0780, 0x0730, 0x0700, 0x0710, 0x0710,
		0x0800, 0x0800, 0x0800, 0x0800, 0x0800, 0x0800, 0x0800, 0x0800,
		0x0800, 0x0800, 0x0800, 0x0800, 0x0800, 0x0800, 0x0800, 0x0800,
	},
	[BATTERY_C18_SUNWODA] = {
		0x9d70, 0xaf80, 0xb6b0, 0xb830, 0xb990, 0xbc00, 0xbcd0, 0xbea0,
		0xc080, 0xc2e0, 0xc5f0, 0xc890, 0xcb90, 0xcf10, 0xd270, 0xd9e0,
		0x0060, 0x0240, 0x0b20, 0x1210, 0x0f20, 0x2200, 0x1650, 0x14f0,
		0x0980, 0x09c0, 0x07b0, 0x07f0, 0x06f0, 0x07e0, 0x05c0, 0x05c0,
		0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400,
		0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400,
	},
	[BATTERY_C19_SUNWODA] = {
		0x8590, 0xb1d0, 0xb810, 0xbae0, 0xbc30, 0xbd70, 0xbeb0, 0xbfa0,
		0xc0f0, 0xc330, 0xc640, 0xc890, 0xcb50, 0xce20, 0xd370, 0xd950,
		0x0020, 0x0520, 0x0d80, 0x1860, 0x1910, 0x2040, 0x0be0, 0x0dd0,
		0x0cb0, 0x07b0, 0x08f0, 0x07c0, 0x0790, 0x06e0, 0x0620, 0x0620,
		0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400,
		0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400, 0x0400,
	},
};
BUILD_ASSERT(ARRAY_SIZE(full_model_ocv_table) == BATTERY_COUNT);

/*
 * TODO: Only precharge_current is different. We should consolidate these
 * and apply 294 or 327 at run-time.when we need more rom space later.
 */
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
	[BATTERY_C18_SUNWODA] = {
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
	[BATTERY_C19_SUNWODA] = {
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
		.ichg_term		= 0x03c0, /* 150 mA */
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
		.ocv_table		= full_model_ocv_table[BATTERY_C18_ATL],
	},
	[BATTERY_C19_ATL] = {
		.is_ez_config		= 0,
		.design_cap		= 0x3407, /* 6659mAh */
		.ichg_term		= 0x03c0, /* 150 mA */
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
		.ocv_table		= full_model_ocv_table[BATTERY_C19_ATL],
	},
	[BATTERY_C18_SUNWODA] = {
		.is_ez_config		= 0,
		.design_cap		= 0x2fcc, /* 6118mAh */
		.ichg_term		= 0x03c0, /* 150 mA */
		/* Empty voltage = 3400mV, Recovery voltage = 4000mV */
		.v_empty_detect		= 0xaa64,
		.learn_cfg		= 0x4402,
		.dpacc			= 0x0c7c,
		.rcomp0			= 0x0024,
		.tempco			= 0x0c1f,
		.qr_table00		= 0x9f00,
		.qr_table10		= 0x4480,
		.qr_table20		= 0x1600,
		.qr_table30		= 0x1400,
		.ocv_table	= full_model_ocv_table[BATTERY_C18_SUNWODA],
	},
	[BATTERY_C19_SUNWODA] = {
		.is_ez_config		= 0,
		.design_cap		= 0x34b1, /* 6744mAh */
		.ichg_term		= 0x03c0, /* 150 mA */
		/* Empty voltage = 3400mV, Recovery voltage = 4000mV */
		.v_empty_detect		= 0xaa64,
		.learn_cfg		= 0x4402,
		.dpacc			= 0x0c80,
		.rcomp0			= 0x001f,
		.tempco			= 0x051f,
		.qr_table00		= 0x9100,
		.qr_table10		= 0x3d00,
		.qr_table20		= 0x1200,
		.qr_table30		= 0x1002,
		.ocv_table	= full_model_ocv_table[BATTERY_C19_SUNWODA],
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
	[BATTERY_C18_SUNWODA] = {
		.v_alert_mxmn = VALRT_DISABLE,
		.t_alert_mxmn = MAX17055_TALRTTH_REG(
			BATTERY_SUNWODA_CHARGE_MIN_TEMP,
			BATTERY_SUNWODA_CHARGE_MAX_TEMP),
		.s_alert_mxmn = SALRT_DISABLE,
		.i_alert_mxmn = IALRT_DISABLE,
	},
	[BATTERY_C19_SUNWODA] = {
		.v_alert_mxmn = VALRT_DISABLE,
		.t_alert_mxmn = MAX17055_TALRTTH_REG(
			BATTERY_SUNWODA_CHARGE_MIN_TEMP,
			BATTERY_SUNWODA_CHARGE_MAX_TEMP),
		.s_alert_mxmn = SALRT_DISABLE,
		.i_alert_mxmn = IALRT_DISABLE,
	},

};
BUILD_ASSERT(ARRAY_SIZE(alert_profile) == BATTERY_COUNT);

enum temp_zone {
	TEMP_ZONE_0, /* t0 <= bat_temp_c < t1  */
	TEMP_ZONE_1, /* t1 <= bat_temp_c < t2 */
	TEMP_ZONE_2, /* t2 <= bat_temp_c < t3 */
	TEMP_ZONE_3, /* t3 <= bat_temp_c < t4 */
	TEMP_ZONE_COUNT,
	TEMP_OUT_OF_RANGE = TEMP_ZONE_COUNT,
};

/*
 * TODO: Many value in temp_zones are pretty similar, we should consolidate
 * these and modify the value when we need more rom space later.
 */
static const struct {
	int temp_min; /* 0.1 deg C */
	int temp_max; /* 0.1 deg C */
	int desired_current; /* mA */
	int desired_voltage; /* mV */
} temp_zones[BATTERY_COUNT][TEMP_ZONE_COUNT] = {
	[BATTERY_C18_ATL] = {
		{BATTERY_ATL_CHARGE_MIN_TEMP * 10, 100, 1170, 4400},
		{100, 200, 1755, 4400},
		{200, 450, 2925, 4400},
		{450, BATTERY_ATL_CHARGE_MAX_TEMP * 10, 2925, 4100},
	},
	[BATTERY_C19_ATL] = {
		{BATTERY_ATL_CHARGE_MIN_TEMP * 10, 100, 1300, 4400},
		{100, 200, 1950, 4400},
		{200, 450, 3250, 4400},
		{450, BATTERY_ATL_CHARGE_MAX_TEMP * 10, 3250, 4100},
	},
	[BATTERY_C18_SUNWODA] = {
		{BATTERY_SUNWODA_CHARGE_MIN_TEMP * 10, 100, 1170, 4400},
		{100, 200, 1755, 4400},
		{200, 450, 2925, 4400},
		{450, BATTERY_SUNWODA_CHARGE_MAX_TEMP * 10, 2925, 4100},
	},
	[BATTERY_C19_SUNWODA] = {
		{BATTERY_SUNWODA_CHARGE_MIN_TEMP * 10, 100, 1300, 4400},
		{100, 200, 1950, 4400},
		{200, 450, 3250, 4400},
		{450, BATTERY_SUNWODA_CHARGE_MAX_TEMP * 10, 3250, 4100},
	},
};

/* BOARD_VERSION < 5: Pull-up = 1800 mV. */
static const struct mv_to_id batteries0[] = {
	{ BATTERY_C18_ATL,	900 },   /* 100K ohm */
	{ BATTERY_C19_ATL,	576 },   /*  47K ohm */
	{ BATTERY_C18_SUNWODA,	1484 },   /* 470K ohm */
	{ BATTERY_C19_SUNWODA,	1200 },   /* 200K ohm */
};
BUILD_ASSERT(ARRAY_SIZE(batteries0) < BATTERY_COUNT);

/* BOARD_VERSION >= 5: Pull-up = 3300 mV. */
static const struct mv_to_id batteries1[] = {
	{ BATTERY_C18_ATL,	1650 },   /* 100K ohm */
	{ BATTERY_C19_ATL,	1055 },   /*  47K ohm */
	{ BATTERY_C18_SUNWODA,	2721 },   /* 470K ohm */
	{ BATTERY_C19_SUNWODA,	2200 },   /* 200K ohm */
};
BUILD_ASSERT(ARRAY_SIZE(batteries1) < BATTERY_COUNT);

static enum battery_type batt_type = BATTERY_UNKNOWN;

static void board_get_battery_type(void)
{
	const struct mv_to_id *table = batteries0;
	int size = ARRAY_SIZE(batteries0);
	int id;

	if (board_version >= 5) {
		table = batteries1;
		size = ARRAY_SIZE(batteries1);
	}
	id = board_read_id(ADC_BATT_ID, table, size);
	if (id != ADC_READ_ERROR)
		batt_type = id;
	CPRINTS("Battery Type: %d", batt_type);
}
/* It has to run after BOARD_VERSION is read */
DECLARE_HOOK(HOOK_INIT, board_get_battery_type, HOOK_PRIO_INIT_I2C + 2);

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

int get_battery_manufacturer_name(char *dest, int size)
{
	static const char * const name[] = {
		[BATTERY_UNKNOWN] = "UNKNOWN",
		[BATTERY_C18_ATL] = "C18_ATL",
		[BATTERY_C19_ATL] = "C19_ATL",
		[BATTERY_C18_SUNWODA] = "C18_SWD",
		[BATTERY_C19_SUNWODA] = "C19_SWD",
	};
	ASSERT(dest);
	strzcpy(dest, name[batt_type], size);
	return EC_SUCCESS;
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
	int temp = curr->batt.temperature - 2731;
	enum temp_zone zone;
	int usb_mv, wpc_mv;
	static int previous_usb_mv;
	int val;

	if (curr->state != ST_CHARGE)
		return 0;

	/* Limit input (=VBUS) to 5V when soc > 85% and charge current < 1A. */
	if (!(curr->batt.flags & BATT_FLAG_BAD_CURRENT) &&
			charge_get_percent() > BAT_LEVEL_PD_LIMIT &&
			curr->batt.current < 1000 &&
			curr->batt.current > 0) {
		usb_mv = 5500;
		wpc_mv = 5500;
	} else {
		usb_mv = PD_MAX_VOLTAGE_MV;
		wpc_mv = P9221_DC_IVL_EPP_MV;
	}

	if (usb_mv != previous_usb_mv)
		CPRINTS("VBUS limited to %dmV", usb_mv);
	previous_usb_mv = usb_mv;

	/* Pull down USB VBUS */
	if (pd_get_max_voltage() != usb_mv)
		pd_set_external_voltage_limit(0, usb_mv);

	/*
	 * Pull down WPC VBUS. Need to use raw i2c APIs because RO
	 * doesn't have p9221 driver. If WPC is off, this is a no-op.
	 */
	if (i2c_read_offset16(I2C_PORT_WPC, P9221_R7_ADDR_FLAGS,
			      P9221R7_VOUT_SET_REG, &val, 1) == EC_SUCCESS
			&& val * 100 != wpc_mv)
		i2c_write_offset16(I2C_PORT_WPC, P9221_R7_ADDR_FLAGS,
			      P9221R7_VOUT_SET_REG, wpc_mv / 100, 1);

	if ((curr->batt.flags & BATT_FLAG_BAD_TEMPERATURE) ||
	    (temp < temp_zones[batt_type][TEMP_ZONE_0].temp_min)) {
		zone = TEMP_OUT_OF_RANGE;
	} else {
		for (zone = TEMP_ZONE_0; zone < TEMP_ZONE_COUNT; zone++) {
			if (temp < temp_zones[batt_type][zone].temp_max)
				break;
		}
	}

	if (zone == TEMP_OUT_OF_RANGE || zone >= TEMP_ZONE_COUNT) {
		curr->requested_current = curr->requested_voltage = 0;
		curr->batt.flags &= ~BATT_FLAG_WANT_CHARGE;
		curr->state = ST_IDLE;
	} else {
		curr->requested_current =
			temp_zones[batt_type][zone].desired_current;
		curr->requested_voltage =
			temp_zones[batt_type][zone].desired_voltage;
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
