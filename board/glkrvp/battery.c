/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "charger_profile_override.h"
#include "console.h"
#include "pca9555.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

#define I2C_PORT_PCA555_BATT_PRESENT_GPIO	NPCX_I2C_PORT0_0
#define I2C_ADDR_PCA555_BATT_PRESENT_GPIO_FLAGS	0x21
#define PCA555_BATT_PRESENT_GPIO_READ(reg, data) \
	pca9555_read(I2C_PORT_PCA555_BATT_PRESENT_GPIO,			\
		     I2C_ADDR_PCA555_BATT_PRESENT_GPIO_FLAGS, (reg), (data))

/* Shutdown mode parameter to write to manufacturer access register */
#define SB_SHUTDOWN_DATA        0x0010

enum fast_chg_voltage_ranges {
	VOLTAGE_RANGE_0,
	VOLTAGE_RANGE_1,
	VOLTAGE_RANGE_2,
};

enum temp_range {
	TEMP_RANGE_0,
	TEMP_RANGE_1,
	TEMP_RANGE_2,
	TEMP_RANGE_3,
	TEMP_RANGE_4,
	TEMP_RANGE_5,
};

/* keep track of previous charge profile info */
static const struct fast_charge_profile *prev_chg_profile_info;

/* SMP-CA-445 battery & BQ30Z554 fuel gauge */
static const struct battery_info batt_info_smp_ca445 = {
	.voltage_max = 8700,		/* mV */
	.voltage_normal = 7600,

	/*
	 * Actual value 6000mV, added 100mV for charger accuracy so that
	 * unwanted low VSYS_Prochot# assertion can be avoided.
	 */
	.voltage_min = 6100,
	.precharge_current = 150,	/* mA */
	.start_charging_min_c = 0,
	.start_charging_max_c = 45,
	.charging_min_c = 0,
	.charging_max_c = 45,
	.discharging_min_c = -20,
	.discharging_max_c = 60,
};

const struct battery_info *battery_get_info(void)
{
	static struct battery_info batt_info;

	if (battery_is_present() == BP_YES)
		return &batt_info_smp_ca445;

	/*
	 * In no battery condition, to avoid voltage drop on VBATA set
	 * the battery minimum voltage to the battery maximum voltage.
	 */

	batt_info = batt_info_smp_ca445;
	batt_info.voltage_min = batt_info.voltage_max;

	return &batt_info;
}

static const struct fast_charge_profile fast_charge_smp_ca445_info[] = {
	/* < 0C */
	[TEMP_RANGE_0] = {
		.temp_c = TEMPC_TENTHS_OF_DEG(-1),
		.current_mA = {
			[VOLTAGE_RANGE_0] = 0,
			[VOLTAGE_RANGE_1] = 0,
			[VOLTAGE_RANGE_2] = 0,
		},
	},

	/* 0C >= && <=15C */
	[TEMP_RANGE_1] = {
		.temp_c = TEMPC_TENTHS_OF_DEG(15),
		.current_mA = {
			[VOLTAGE_RANGE_0] = 890,
			[VOLTAGE_RANGE_1] = 445,
			[VOLTAGE_RANGE_2] = 445,
		},
	},

	/* 15C > && <=20C */
	[TEMP_RANGE_2] = {
		.temp_c = TEMPC_TENTHS_OF_DEG(20),
		.current_mA = {
			[VOLTAGE_RANGE_0] = 1335,
			[VOLTAGE_RANGE_1] = 1335,
			[VOLTAGE_RANGE_2] = 1335,
		},
	},

	/* 20C > && <=45C */
	[TEMP_RANGE_3] = {
		.temp_c = TEMPC_TENTHS_OF_DEG(45),
		.current_mA = {
			[VOLTAGE_RANGE_0] = 2225,
			[VOLTAGE_RANGE_1] = 2225,
			[VOLTAGE_RANGE_2] = 2225,
		},
	},

	/* 45C > && <=55C */
	[TEMP_RANGE_4] = {
		.temp_c = TEMPC_TENTHS_OF_DEG(55),
		.current_mA = {
			[VOLTAGE_RANGE_0] = 1335,
			[VOLTAGE_RANGE_1] = 1335,
			[VOLTAGE_RANGE_2] = 0,
		},
	},

	/* > 55C */
	[TEMP_RANGE_5] = {
		.temp_c = TEMPC_TENTHS_OF_DEG(CHARGER_PROF_TEMP_C_LAST_RANGE),
		.current_mA = {
			[VOLTAGE_RANGE_0] = 0,
			[VOLTAGE_RANGE_1] = 0,
			[VOLTAGE_RANGE_2] = 0,
		},
	},
};

static const struct fast_charge_params fast_chg_params_smp_ca445 = {
	.total_temp_ranges = ARRAY_SIZE(fast_charge_smp_ca445_info),
	.default_temp_range_profile = TEMP_RANGE_3,
	.voltage_mV = {
		[VOLTAGE_RANGE_0] = 8000,
		[VOLTAGE_RANGE_1] = 8200,
		[VOLTAGE_RANGE_2] = CHARGER_PROF_VOLTAGE_MV_LAST_RANGE,
	},
	.chg_profile_info = &fast_charge_smp_ca445_info[0],
};

/*
 * This can override the smart battery's charging profile. To make a change,
 * modify one or more of requested_voltage, requested_current, or state.
 * Leave everything else unchanged.
 *
 * Return the next poll period in usec, or zero to use the default (which is
 * state dependent).
 */
int charger_profile_override(struct charge_state_data *curr)
{
	/*
	 * If battery present and not in cut off and almost full
	 * then if it does not want charge then discharge on AC
	 */
	if ((battery_is_present() == BP_YES) &&
	    !(curr->batt.flags & BATT_FLAG_WANT_CHARGE) &&
	    (curr->batt.status & STATUS_FULLY_CHARGED)) {
		charger_discharge_on_ac(1);
		curr->state = ST_DISCHARGE;
		return 0;
	}

	charger_discharge_on_ac(0);

	return charger_profile_override_common(curr,
			&fast_chg_params_smp_ca445,
			&prev_chg_profile_info,
			batt_info_smp_ca445.voltage_max);
}

int board_cut_off_battery(void)
{
	int rv;

	/* Ship mode command must be sent twice to take effect */
	rv = sb_write(SB_MANUFACTURER_ACCESS, SB_SHUTDOWN_DATA);
	if (rv != EC_SUCCESS)
		return rv;

	return sb_write(SB_MANUFACTURER_ACCESS, SB_SHUTDOWN_DATA);
}

static inline int batt_smp_cos4870_is_initialized(void)
{
	int batt_status;

	return battery_status(&batt_status) ? 0 :
		batt_status & STATUS_INITIALIZED;
}

enum battery_present battery_hw_present(void)
{
	int data;
	int rv;

	rv = PCA555_BATT_PRESENT_GPIO_READ(PCA9555_CMD_INPUT_PORT_0, &data);

	/* GPIO is low when the battery is physically present */
	return rv || (data & PCA9555_IO_5) ? BP_NO : BP_YES;
}

/*
 * Physical detection of battery.
 */
enum battery_present battery_is_present(void)
{
	static enum battery_present batt_pres_prev = BP_NOT_SURE;
	enum battery_present batt_pres;

	/* Get the physical hardware status */
	batt_pres = battery_hw_present();

	/*
	 * Make sure battery status is implemented, I2C transactions are
	 * success & the battery status is Initialized to find out if it
	 * is a working battery and it is not in the cut-off mode.
	 *
	 * FETs are turned off after Power Shutdown time.
	 * The device will wake up when a voltage is applied to PACK.
	 * Battery status will be inactive until it is initialized.
	 */
	if (batt_pres == BP_YES && batt_pres_prev != batt_pres &&
		!battery_is_cut_off() && !batt_smp_cos4870_is_initialized())
		batt_pres = BP_NO;

	batt_pres_prev = batt_pres;

	return batt_pres;
}
