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
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

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

const struct battery_info *battery_get_info(void)
{
	int i;
	char manuf[9];
	char device[9];
	int design_mv;

	if (battery_manufacturer_name(manuf, sizeof(manuf))) {
		CPRINTS("Failed to get MANUF name");
		return &info_precharge;
	}

	if (battery_device_name(device, sizeof(device))) {
		CPRINTS("Failed to get DEVICE name");
		return &info_precharge;
	}
	if (battery_design_voltage((int *)&design_mv)) {
		CPRINTS("Failed to get DESIGN_VOLTAGE");
		return &info_precharge;
	}

	for (i = 0; i < ARRAY_SIZE(support_batteries); ++i) {
		if ((strcasecmp(support_batteries[i].manuf, manuf) == 0) &&
		    (strcasecmp(support_batteries[i].device, device) == 0) &&
		    (support_batteries[i].design_mv == design_mv)) {
			CPRINTS("battery Manuf:%s, Device=%s, design=%u",
				manuf, device, design_mv);
			support_cut_off = support_batteries[i].support_cut_off;
			battery_info = support_batteries[i].battery_info;
			return battery_info;
		}
	}

	CPRINTS("un-recognized battery Manuf:%s, Device:%s",
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
