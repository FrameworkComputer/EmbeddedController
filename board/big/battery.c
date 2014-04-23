/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "battery_smart.h"
#include "gpio.h"
#include "host_command.h"
#include "util.h"
#include "console.h"

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

/* These 2 defines are for cut_off command for 3S battery */
#define SB_SHIP_MODE_ADDR	0x3a
#define SB_SHIP_MODE_DATA	0xc574

static struct battery_info *battery_info;
static int support_cut_off;

struct battery_device {
	char			manuf[9];
	char			device[9];
	int			design_mv;
	struct battery_info	*battery_info;
	int			support_cut_off;
};

/*
 * Used for the case that battery cannot be detected, such as the pre-charge
 * case. In this case, we need to provide the battery with the enough voltage
 * (usually the highest voltage among batteries, but the smallest precharge
 * current). This should be as conservative as possible.
 */
static struct battery_info info_precharge = {

	.voltage_max    = 12900,  /* the max voltage among batteries */
	.voltage_normal = 11400,
	.voltage_min    =  9000,

	/* Pre-charge values. */
	.precharge_current  = 256,  /* mA, the min current among batteries */

	.start_charging_min_c = 0,
	.start_charging_max_c = 50,
	.charging_min_c       = 0,
	.charging_max_c       = 60,
	.discharging_min_c    = 0,
	.discharging_max_c    = 75,
};

static struct battery_info info_2s = {
	/*
	 * Design voltage
	 *   max    = 8.4V
	 *   normal = 7.4V
	 *   min    = 6.0V
	 */
	.voltage_max    = 8400,
	.voltage_normal = 7400,
	.voltage_min    = 6000,

	/* Pre-charge current: I <= 0.01C */
	.precharge_current  = 64, /* mA */

	/*
	 * Operational temperature range
	 *   0 <= T_charge    <= 50 deg C
	 * -20 <= T_discharge <= 60 deg C
	 */
	.start_charging_min_c = 0,
	.start_charging_max_c = 50,
	.charging_min_c       = 0,
	.charging_max_c       = 50,
	.discharging_min_c    = -20,
	.discharging_max_c    = 60,
};

static struct battery_info info_3s = {

	.voltage_max    = 12600,
	.voltage_normal = 11100, /* Average of max & min */
	.voltage_min    =  9000,

	/* Pre-charge values. */
	.precharge_current  = 392,	/* mA */

	.start_charging_min_c = 0,
	.start_charging_max_c = 60,
	.charging_min_c       = 0,
	.charging_max_c       = 60,
	.discharging_min_c    = 0,
	.discharging_max_c    = 50,
};

static struct battery_info info_3s_LGC = {

        .voltage_max    = 12900,
        .voltage_normal = 11400, /* Average of max & min */
        .voltage_min    =  9000,

        /* Pre-charge values. */
        .precharge_current  = 256,      /* mA */

        .start_charging_min_c = 0,
        .start_charging_max_c = 50,
        .charging_min_c       = 0,
        .charging_max_c       = 60,
        .discharging_min_c    = 0,
        .discharging_max_c    = 75,
};

static struct battery_info info_4s_LGC = {

	.voltage_max    = 17200,
	.voltage_normal = 15200, /* Average of max & min */
	.voltage_min    = 12000,

	/* Pre-charge values. */
	.precharge_current  = 256,	/* mA */

	.start_charging_min_c = 0,
	.start_charging_max_c = 50,
	.charging_min_c       = 0,
	.charging_max_c       = 60,
	.discharging_min_c    = 0,
	.discharging_max_c    = 75,
};

static struct battery_device support_batteries[] = {
	{
		.manuf			= "NVT",
		.device			= "ARROW",
		.design_mv		= 7400,
		.battery_info		= &info_2s,
		.support_cut_off	= 0,
	},
	{
		.manuf			= "SANYO",
		.device			= "AP13J3K",
		.design_mv		= 11250,
		.battery_info		= &info_3s,
		.support_cut_off	= 1,
	},
	{
		.manuf			= "SONYCorp",
		.device			= "AP13J4K",
		.design_mv		= 11400,
		.battery_info		= &info_3s,
		.support_cut_off	= 1,
	},
	{
		.manuf			= "LGC",
		.device			= "AC14B8K",
		.design_mv		= 15200,
		.battery_info		= &info_4s_LGC,
		.support_cut_off	= 1,
	},
	{
		.manuf                  = "LGC",
		.device                 = "AC14B18J",
		.design_mv              = 11400,
		.battery_info           = &info_3s_LGC,
		.support_cut_off        = 1,
	},
};

#ifdef CONFIG_BATTERY_VENDOR_PARAMS
/*
 * The following parameters are for 2S battery.
 * There is no corresponding params for 3S battery.
 */
enum {
	TEMP_RANGE_10,
	TEMP_RANGE_23,
	TEMP_RANGE_35,
	TEMP_RANGE_45,
	TEMP_RANGE_50,
	TEMP_RANGE_MAX
};

enum {
	VOLT_RANGE_7200,
	VOLT_RANGE_8000,
	VOLT_RANGE_8400,
	VOLT_RANGE_MAX
};

/*
 * Vendor provided charging method
 *      temp  : < 7.2V, 7.2V ~ 8.0V, 8.0V ~ 8.4V
 *  -  0 ~ 10 :  0.8A       1.6A         0.8A
 *  - 10 ~ 23 :  1.6A       4.0A         1.6A
 *  - 23 ~ 35 :  4.0A       4.0A         4.0A
 *  - 35 ~ 45 :  1.6A       4.0A         1.6A
 *  - 45 ~ 50 :  0.8A       1.6A         0.8A
 */
static const int const current_limit[TEMP_RANGE_MAX][VOLT_RANGE_MAX] = {
	{ 800, 1600,  800},
	{1600, 4000, 1600},
	{4000, 4000, 4000},
	{1600, 4000, 1600},
	{ 800, 1600,  800},
};

static inline void limit_value(int *val, int limit)
{
	if (*val > limit)
		*val = limit;
}

void battery_vendor_params(struct batt_params *batt)
{
	int *desired_current = &batt->desired_current;
	int temp_range, volt_range;
	int bat_temp_c = DECI_KELVIN_TO_CELSIUS(batt->temperature);

	if (battery_info == NULL)
		return;

	/* Return if the battery is not a 2S battery */
	if (battery_info->voltage_max != info_2s.voltage_max)
		return;

	/* Limit charging voltage */
	if (batt->desired_voltage > battery_info->voltage_max)
		batt->desired_voltage = battery_info->voltage_max;

	/* Don't charge if outside of allowable temperature range */
	if (bat_temp_c >= battery_info->charging_max_c ||
	    bat_temp_c < battery_info->charging_min_c) {
		batt->desired_voltage = 0;
		batt->desired_current = 0;
		batt->flags &= ~BATT_FLAG_WANT_CHARGE;
		return;
	}

	if (bat_temp_c <= 10)
		temp_range = TEMP_RANGE_10;
	else if (bat_temp_c <= 23)
		temp_range = TEMP_RANGE_23;
	else if (bat_temp_c <= 35)
		temp_range = TEMP_RANGE_35;
	else if (bat_temp_c <= 45)
		temp_range = TEMP_RANGE_45;
	else
		temp_range = TEMP_RANGE_50;

	if (batt->voltage < 7200)
		volt_range = VOLT_RANGE_7200;
	else if (batt->voltage < 8000)
		volt_range = VOLT_RANGE_8000;
	else
		volt_range = VOLT_RANGE_8400;

	limit_value(desired_current, current_limit[temp_range][volt_range]);

	/* If battery wants current, give it at least the precharge current */
	if (*desired_current > 0 &&
	    *desired_current < battery_info->precharge_current)
		*desired_current = battery_info->precharge_current;
}
#endif	/* CONFIG_BATTERY_VENDOR_PARAMS */

const struct battery_info *battery_get_info(void)
{
	int i;
	char manuf[9];
	char device[9];
	int design_mv;

	if (battery_manufacturer_name(manuf, sizeof(manuf))) {
		CPRINTF("[%T Failed to get MANUF name]\n");
		return &info_precharge;
	}

	if (battery_device_name(device, sizeof(device))) {
		CPRINTF("[%T Failed to get DEVICE name]\n");
		return &info_precharge;
	}
	if (battery_design_voltage((int *)&design_mv)) {
		CPRINTF("[%T Failed to get DESIGN_VOLTAGE]\n");
		return &info_precharge;
	}

	for (i = 0; i < ARRAY_SIZE(support_batteries); ++i) {
		if ((strcasecmp(support_batteries[i].manuf, manuf) == 0) &&
		    (strcasecmp(support_batteries[i].device, device) == 0) &&
		    (support_batteries[i].design_mv == design_mv)) {
			CPRINTF("[%T battery Manuf:%s, Device=%s, design=%u]\n",
				manuf, device, design_mv);
			support_cut_off = support_batteries[i].support_cut_off;
			battery_info = support_batteries[i].battery_info;
			return battery_info;
		}
	}

	CPRINTF("[%T un-recognized battery Manuf:%s, Device:%s]\n",
		manuf, device);
	return &info_precharge;
}

int board_cut_off_battery(void)
{
	if (support_cut_off)
		return sb_write(SB_SHIP_MODE_ADDR, SB_SHIP_MODE_DATA);
	else
		return EC_RES_INVALID_COMMAND;
}
