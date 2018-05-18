/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Placeholder values for temporary battery pack.
 */

#include "battery.h"
#include "battery_smart.h"
#include "bd9995x.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "console.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

/* Shutdown mode parameter to write to manufacturer access register */
#define SB_SHUTDOWN_DATA	0x0010

enum battery_type {
	BATTERY_LG,
	BATTERY_LISHEN,
	BATTERY_SIMPLO,
	BATTERY_TYPE_COUNT,
};

struct board_batt_params {
	const char *manuf_name;
	const struct battery_info *batt_info;
};

/*
 * Set LISHEN as default since the LG precharge current level could cause the
 * LISHEN battery to not accept charge when it's recovering from a fully
 * discharged state.
 */
#define DEFAULT_BATTERY_TYPE BATTERY_LISHEN
static enum battery_present batt_pres_prev = BP_NOT_SURE;
static enum battery_type board_battery_type = BATTERY_TYPE_COUNT;

/* Battery may delay reporting battery present */
static int battery_report_present = 1;

/*
 * Battery info for LG A50. Note that the fields start_charging_min/max and
 * charging_min/max are not used for the Eve charger. The effective temperature
 * limits are given by discharging_min/max_c.
 */
static const struct battery_info batt_info_lg = {
	.voltage_max		= TARGET_WITH_MARGIN(8800, 5), /* mV */
	.voltage_normal		= 7700,
	.voltage_min		= 6100, /* Add 100mV for charger accuracy */
	.precharge_current	= 256,	/* mA */
	.start_charging_min_c	= 0,
	.start_charging_max_c	= 46,
	.charging_min_c		= 10,
	.charging_max_c		= 50,
	.discharging_min_c	= 0,
	.discharging_max_c	= 60,
};

/*
 * Battery info for LISHEN. Note that the fields start_charging_min/max and
 * charging_min/max are not used for the Eve charger. The effective temperature
 * limits are given by discharging_min/max_c.
 */
static const struct battery_info batt_info_lishen = {
	.voltage_max		= TARGET_WITH_MARGIN(8800, 5), /* mV */
	.voltage_normal		= 7700,
	.voltage_min		= 6100, /* Add 100mV for charger accuracy */
	.precharge_current	= 256,	/* mA */
	.start_charging_min_c	= 0,
	.start_charging_max_c	= 46,
	.charging_min_c		= 10,
	.charging_max_c		= 50,
	.discharging_min_c	= 0,
	.discharging_max_c	= 60,
};

static const struct board_batt_params info[] = {
	[BATTERY_LG] = {
		.manuf_name = "LG A50",
		.batt_info = &batt_info_lg,
	},

	[BATTERY_LISHEN] = {
		.manuf_name = "Lishen A50",
		.batt_info = &batt_info_lishen,
	},

	[BATTERY_SIMPLO] = {
		.manuf_name = "Simplo A50",
		.batt_info = &batt_info_lishen,
	},

};
BUILD_ASSERT(ARRAY_SIZE(info) == BATTERY_TYPE_COUNT);

/* Get type of the battery connected on the board */
static int board_get_battery_type(void)
{
	char name[3];
	int i;

	if (!battery_manufacturer_name(name, sizeof(name))) {
		for (i = 0; i < BATTERY_TYPE_COUNT; i++) {
			if (!strncasecmp(name, info[i].manuf_name,
					 ARRAY_SIZE(name)-1)) {
				board_battery_type = i;
				break;
			}
		}
	}

	return board_battery_type;
}

/*
 * Initialize the battery type for the board.
 *
 * Very first battery info is called by the charger driver to initialize
 * the charger parameters hence initialize the battery type for the board
 * as soon as the I2C is initialized.
 */
static void board_init_battery_type(void)
{
	if (board_get_battery_type() != BATTERY_TYPE_COUNT)
		CPRINTS("found batt: %s", info[board_battery_type].manuf_name);
	else
		CPRINTS("battery not found");
}
DECLARE_HOOK(HOOK_INIT, board_init_battery_type, HOOK_PRIO_INIT_I2C + 1);

const struct battery_info *battery_get_info(void)
{
	return info[board_battery_type == BATTERY_TYPE_COUNT ?
		    DEFAULT_BATTERY_TYPE : board_battery_type].batt_info;
}

int board_cut_off_battery(void)
{
	int rv;

	/* Ship mode command must be sent twice to take effect */
	rv = sb_write(SB_MANUFACTURER_ACCESS, SB_SHUTDOWN_DATA);
	if (rv != EC_SUCCESS)
		return EC_RES_ERROR;

	rv = sb_write(SB_MANUFACTURER_ACCESS, SB_SHUTDOWN_DATA);
	return rv ? EC_RES_ERROR : EC_RES_SUCCESS;
}

int charger_profile_override(struct charge_state_data *curr)
{
	const struct battery_info *batt_info;
	/* battery temp in 0.1 deg C */
	int bat_temp_c;

	if (curr->batt.flags & BATT_FLAG_BAD_TEMPERATURE)
		return 0;

	bat_temp_c = curr->batt.temperature - 2731;
	batt_info = battery_get_info();
	/* Don't charge if outside of allowable temperature range */
	if (bat_temp_c >= batt_info->charging_max_c * 10 ||
	    bat_temp_c < batt_info->charging_min_c * 10) {
		curr->requested_current = 0;
		curr->requested_voltage = 0;
		curr->batt.flags &= ~BATT_FLAG_WANT_CHARGE;
		curr->state = ST_IDLE;
	}
	return 0;
}

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

enum battery_present battery_hw_present(void)
{
	/* The GPIO is low when the battery is physically present */
	return gpio_get_level(GPIO_BATTERY_PRESENT_L) ? BP_NO : BP_YES;
}

static int battery_init(void)
{
	int batt_status;

	return battery_status(&batt_status) ? 0 :
		!!(batt_status & STATUS_INITIALIZED);
}

/* Allow booting now that the battery has woke up */
static void battery_now_present(void)
{
	CPRINTS("battery will now report present");
	battery_report_present = 1;
}
DECLARE_DEFERRED(battery_now_present);

/*
 * Check for case where XDSG bit is set indicating that even
 * though the FG can be read from the battery, the battery is not able to be
 * charged or discharged. This situation will happen if a battery disconnect was
 * intiaited via H1 setting the DISCONN signal to the battery. This will put the
 * battery pack into a sleep state and when power is reconnected, the FG can be
 * read, but the battery is still not able to provide power to the system. The
 * calling function returns batt_pres = BP_NO, which instructs the charging
 * state machine to prevent powering up the AP on battery alone which could lead
 * to a brownout event when the battery isn't able yet to provide power to the
 * system. .
 */
static int battery_check_disconnect(void)
{
	int rv;
	uint8_t data[6];

	/* Check if battery discharging is disabled. */
	rv = sb_read_mfgacc(PARAM_OPERATION_STATUS,
			    SB_ALT_MANUFACTURER_ACCESS, data, sizeof(data));
	if (rv)
		return BATTERY_DISCONNECT_ERROR;

	if (data[3] & BATTERY_DISCHARGING_DISABLED)
		return BATTERY_DISCONNECTED;

	return BATTERY_NOT_DISCONNECTED;
}

/*
 * Physical detection of battery.
 */
enum battery_present battery_is_present(void)
{
	enum battery_present batt_pres;
	static int battery_report_present_timer_started;

	/* Get the physical hardware status */
	batt_pres = battery_hw_present();

	/*
	 * Make sure battery status is implemented, I2C transactions are
	 * success & the battery status is Initialized to find out if it
	 * is a working battery and it is not in the cut-off mode.
	 *
	 * If battery I2C fails but VBATT is high, battery is booting from
	 * cut-off mode.
	 *
	 * FETs are turned off after Power Shutdown time.
	 * The device will wake up when a voltage is applied to PACK.
	 * Battery status will be inactive until it is initialized.
	 */
	if (batt_pres == BP_YES && batt_pres_prev != batt_pres &&
	    (battery_is_cut_off() != BATTERY_CUTOFF_STATE_NORMAL ||
	     battery_check_disconnect() != BATTERY_NOT_DISCONNECTED ||
	     battery_init() == 0)) {
		battery_report_present = 0;
	} else if (batt_pres == BP_YES && batt_pres_prev == BP_NO &&
		   !battery_report_present_timer_started) {
		/*
		 * Wait 1 second before reporting present if it was
		 * previously reported as not present
		 */
		battery_report_present_timer_started = 1;
		battery_report_present = 0;
		hook_call_deferred(&battery_now_present_data, SECOND);
	}

	if (!battery_report_present)
		batt_pres = BP_NO;

	batt_pres_prev = batt_pres;

	return batt_pres;
}

int board_battery_initialized(void)
{
	return battery_hw_present() == batt_pres_prev;
}
