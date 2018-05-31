/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Placeholder values for temporary battery pack.
 */

#include "battery.h"
#include "battery_smart.h"
#include "board.h"
#include "charge_state.h"
#include "console.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "util.h"

/* Default, Nami, Vayne */
static const struct battery_info info_0 = {
	.voltage_max = 8800,
	.voltage_normal = 7600,
	.voltage_min = 6000,
	.precharge_current = 256,
	.start_charging_min_c = 0,
	.start_charging_max_c = 50,
	.charging_min_c = 0,
	.charging_max_c = 60,
	.discharging_min_c = -20,
	.discharging_max_c = 70,
};

/* Sona */
static const struct battery_info info_1 = {
	.voltage_max = 13200,
	.voltage_normal = 11550,
	.voltage_min = 9000,
	.precharge_current = 256,
	.start_charging_min_c = 0,
	.start_charging_max_c = 45,
	.charging_min_c = 0,
	.charging_max_c = 45,
	.discharging_min_c = -10,
	.discharging_max_c = 60,
};

/* Pantheon */
static const struct battery_info info_2 = {
	.voltage_max = 8700,
	.voltage_normal = 7500,
	.voltage_min = 6000,
	.precharge_current = 373,
	.start_charging_min_c = 0,
	.start_charging_max_c = 50,
	.charging_min_c = 0,
	.charging_max_c = 60,
	.discharging_min_c = -20,
	.discharging_max_c = 60,
};

/* Panasonic AP15O5L (Akali) */
static const struct battery_info info_3 = {
	.voltage_max = 13200,
	.voltage_normal = 11550,
	.voltage_min = 9000,
	.precharge_current = 256,
	.start_charging_min_c = 0,
	.start_charging_max_c = 50,
	.charging_min_c = 0,
	.charging_max_c = 60,
	.discharging_min_c = 0,
	.discharging_max_c = 60,
};

static const struct battery_info *info = &info_0;
static int sb_ship_mode_reg = SB_MANUFACTURER_ACCESS;
static int sb_shutdown_data = 0x0010;

const struct battery_info *battery_get_info(void)
{
	return info;
}

void board_battery_init(void)
{
	if (oem == PROJECT_AKALI) {
		info = &info_3;
		sb_ship_mode_reg = 0x3A;
		sb_shutdown_data = 0xC574;
	} else if (oem == PROJECT_SONA)
		info = &info_1;
	else if (oem == PROJECT_PANTHEON)
		info = &info_2;
}
DECLARE_HOOK(HOOK_INIT, board_battery_init, HOOK_PRIO_DEFAULT);

int board_cut_off_battery(void)
{
	int rv;

	/* Ship mode command must be sent twice to take effect */
	rv = sb_write(sb_ship_mode_reg, sb_shutdown_data);

	if (rv != EC_SUCCESS)
		return rv;

	return sb_write(sb_ship_mode_reg, sb_shutdown_data);
}

int charger_profile_override(struct charge_state_data *curr)
{
	const struct battery_info *batt_info;
	int bat_temp_c;

	batt_info = battery_get_info();

	if ((curr->batt.flags & BATT_FLAG_BAD_ANY) == BATT_FLAG_BAD_ANY) {
		curr->requested_current = batt_info->precharge_current;
		curr->requested_voltage = batt_info->voltage_max;
		return 1000;
	}

	/* battery temp in 0.1 deg C */
	bat_temp_c = curr->batt.temperature - 2731;

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

/*
 * Check for case where both XCHG and XDSG bits are set indicating that even
 * though the FG can be read from the battery, the battery is not able to be
 * charged or discharged. This situation will happen if a battery disconnect was
 * initiated via H1 setting the DISCONN signal to the battery. This will put the
 * battery pack into a sleep state and when power is reconnected, the FG can be
 * read, but the battery is still not able to provide power to the system. The
 * calling function returns batt_pres = BP_NO, which instructs the charging
 * state machine to prevent powering up the AP on battery alone which could lead
 * to a brownout event when the battery isn't able yet to provide power to the
 * system. .
 */
static int battery_check_disconnect_0(void)
{
	int rv;
	uint8_t data[6];

	/* Check if battery charging + discharging is disabled. */
	rv = sb_read_mfgacc(PARAM_OPERATION_STATUS,
			    SB_ALT_MANUFACTURER_ACCESS, data, sizeof(data));
	if (rv)
		return BATTERY_DISCONNECT_ERROR;

	/* TODO(dnojiri): Verify if battery supports this check. */
	if ((data[3] & (BATTERY_DISCHARGING_DISABLED |
			BATTERY_CHARGING_DISABLED)) ==
	    (BATTERY_DISCHARGING_DISABLED | BATTERY_CHARGING_DISABLED))
		return BATTERY_DISCONNECTED;

	return BATTERY_NOT_DISCONNECTED;
}

static int battery_check_disconnect_1(void)
{
	int batt_discharge_fet;

	if (sb_read(SB_MANUFACTURER_ACCESS, &batt_discharge_fet))
		return BATTERY_DISCONNECT_ERROR;

	/* Bit 15: Discharge FET status (1: On, 0: Off) */
	if (batt_discharge_fet & 0x4000)
		return BATTERY_NOT_DISCONNECTED;

	return BATTERY_DISCONNECTED;
}

static int battery_check_disconnect(void)
{
	return oem == PROJECT_AKALI ?
		battery_check_disconnect_1() : battery_check_disconnect_0();
}

static enum battery_present batt_pres_prev; /* Default BP_NO (=0) */

static enum battery_present battery_check_present_status(void)
{
	enum battery_present batt_pres;
	int batt_disconnect_status;

	/* Get the physical hardware status */
	batt_pres = battery_hw_present();

	/*
	 * If the battery is not physically connected, then no need to perform
	 * any more checks.
	 */
	if (batt_pres != BP_YES)
		return batt_pres;

	/*
	 * If the battery is present now and was present last time we checked,
	 * return early.
	 */
	if (batt_pres == batt_pres_prev)
		return batt_pres;

	/*
	 * Check battery disconnect status. If we are unable to read battery
	 * disconnect status, then return BP_NOT_SURE. Battery could be in ship
	 * mode and might require pre-charge current to wake it up. BP_NO is not
	 * returned here because charger state machine will not provide
	 * pre-charge current assuming that battery is not present.
	 */
	batt_disconnect_status = battery_check_disconnect();
	if (batt_disconnect_status == BATTERY_DISCONNECT_ERROR)
		return BP_NOT_SURE;

	/*
	 * Ensure that battery is:
	 * 1. Not in cutoff
	 * 2. Not disconnected
	 * 3. Initialized
	 */
	if (battery_is_cut_off() != BATTERY_CUTOFF_STATE_NORMAL ||
	    batt_disconnect_status != BATTERY_NOT_DISCONNECTED ||
	    battery_init() == 0)
		return BP_NO;

	return BP_YES;
}

enum battery_present battery_is_present(void)
{
	batt_pres_prev = battery_check_present_status();
	return batt_pres_prev;
}
