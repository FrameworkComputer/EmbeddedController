/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Placeholder values for temporary battery pack.
 */

#include "battery.h"
#include "battery_smart.h"
#include "charge_state.h"
#include "console.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

static enum battery_present batt_pres_prev = BP_NOT_SURE;

/* Shutdown mode parameter to write to manufacturer access register */
#define SB_SHIP_MODE_REG	SB_MANUFACTURER_ACCESS
#define SB_SHUTDOWN_DATA	0x0010
#define SB_REVIVE_DATA		0x23a7

#if defined(BOARD_SORAKA) || defined(BOARD_LUX)
static const struct battery_info info = {
	.voltage_max = 8800,
	.voltage_normal = 7700,
	.voltage_min = 6100,
	/* Pre-charge values. */
	.precharge_current = 256, /* mA */

	.start_charging_min_c = 0,
	.start_charging_max_c = 45,
	.charging_min_c = 0,
	.charging_max_c = 45,
	.discharging_min_c = -10,
	.discharging_max_c = 60,
};
#elif defined(BOARD_POPPY)

static const struct battery_info info = {
	.voltage_max = 13200,
	.voltage_normal = 11550,
	.voltage_min = 9100,
	/* Pre-charge values. */
	.precharge_current = 256, /* mA */

	.start_charging_min_c = 0,
	.start_charging_max_c = 50,
	.charging_min_c = 0,
	.charging_max_c = 60,
	.discharging_min_c = 0,
	.discharging_max_c = 60,
};
#else
#error "Battery information not available for board"
#endif

const struct battery_info *battery_get_info(void)
{
	return &info;
}

int board_cut_off_battery(void)
{
	int rv;

	/* Ship mode command must be sent twice to take effect */
	rv = sb_write(SB_SHIP_MODE_REG, SB_SHUTDOWN_DATA);

	if (rv != EC_SUCCESS)
		return rv;

	return sb_write(SB_SHIP_MODE_REG, SB_SHUTDOWN_DATA);
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

	/* Check if battery charging + discharging is disabled. */
	rv = sb_read_mfgacc(PARAM_OPERATION_STATUS,
			    SB_ALT_MANUFACTURER_ACCESS, data, sizeof(data));
	if (rv)
		return BATTERY_DISCONNECT_ERROR;

	if ((data[3] & (BATTERY_DISCHARGING_DISABLED |
			BATTERY_CHARGING_DISABLED)) ==
	    (BATTERY_DISCHARGING_DISABLED | BATTERY_CHARGING_DISABLED))
		return BATTERY_DISCONNECTED;

	return BATTERY_NOT_DISCONNECTED;
}

#ifdef BOARD_SORAKA
/*
 * In case of soraka, battery enters an "emergency shutdown" mode when hardware
 * button combo is used to cutoff battery. In order to get out of this mode, EC
 * needs to send SB_REVIVE_DATA.
 *
 * Do not send revive data if:
 * 1. It has already been sent during this boot or
 * 2. Battery was/is in a state other than "BATTERY_DISCONNECTED".
 *
 * Try upto ten times to send the revive data command and if it fails every
 * single time, give up and continue booting on AC power.
 */
static void battery_revive(void)
{
#define MAX_REVIVE_TRIES 10
	static int battery_revive_done;
	int tries = MAX_REVIVE_TRIES;

	if (battery_revive_done)
		return;

	battery_revive_done = 1;

	while (tries--) {
		if (battery_check_disconnect() != BATTERY_DISCONNECTED)
			return;

		CPRINTS("Battery is disconnected! Try#%d to revive",
			MAX_REVIVE_TRIES - tries);

		if (sb_write(SB_MANUFACTURER_ACCESS, SB_REVIVE_DATA) ==
		    EC_SUCCESS)
			return;
	}

	if (battery_check_disconnect() == BATTERY_DISCONNECTED)
		CPRINTS("Battery is still disconnected! Giving up!");
}
#endif

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

#ifdef BOARD_SORAKA
	/*
	 * Since battery just changed status to present and we are able to read
	 * disconnect status, try reviving it if necessary.
	 */
	battery_revive();
#endif

	/*
	 * Ensure that battery is:
	 * 1. Not in cutoff
	 * 2. Not disconnected
	 * 3. Initialized
	 */
	if (battery_is_cut_off() != BATTERY_CUTOFF_STATE_NORMAL ||
	    batt_disconnect_status != BATTERY_NOT_DISCONNECTED ||
	    battery_init() == 0) {
		batt_pres = BP_NO;
	}

	return batt_pres;
}

enum battery_present battery_is_present(void)
{
	batt_pres_prev = battery_check_present_status();
	return batt_pres_prev;
}

